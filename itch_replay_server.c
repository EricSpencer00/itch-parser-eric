/*
 * ITCH Replay Server - High-performance TCP server for streaming historical ITCH data
 * 
 * Features:
 * - Timestamp-accurate replay with configurable speed multiplier
 * - Support for gzip-compressed ITCH files
 * - Multiple concurrent client connections
 * - Zero-copy message streaming where possible
 * 
 * Usage:
 *   ./itch_replay_server <itch_file.bin> [port] [speed_multiplier]
 * 
 * Example:
 *   ./itch_replay_server data/01302019.NASDAQ_ITCH50.gz 9999 1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>
#include <zlib.h>

#define DEFAULT_PORT 9999
#define DEFAULT_SPEED 1.0
#define BUFFER_SIZE (64 * 1024)  // 64KB buffer
#define MAX_CLIENTS 32

/* ITCH message length lookup table */
static const uint8_t ITCH_MSG_LENGTHS[256] = {
    ['S'] = 12, ['R'] = 39, ['H'] = 25, ['Y'] = 20,
    ['L'] = 26, ['V'] = 35, ['W'] = 12, ['K'] = 28,
    ['A'] = 36, ['F'] = 40, ['E'] = 31, ['C'] = 36,
    ['X'] = 23, ['D'] = 19, ['U'] = 35, ['P'] = 44,
    ['Q'] = 40, ['B'] = 19, ['I'] = 50, ['N'] = 20,
};

/* Server configuration */
typedef struct {
    const char *filename;
    int port;
    double speed_multiplier;
    int is_gzip;
} server_config_t;

/* Client connection state */
typedef struct {
    int socket_fd;
    struct sockaddr_in address;
    int active;
} client_t;

/* Global state */
static client_t clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int server_running = 1;

/* Read big-endian uint64 from 6 bytes (timestamp) */
static inline uint64_t read_timestamp(const uint8_t *b) {
    uint64_t tmp;
    memcpy(&tmp, b, 8);
    tmp = __builtin_bswap64(tmp);
    return tmp >> 16;
}

/* Get message length for ITCH message type */
static inline size_t get_message_length(uint8_t msg_type) {
    return ITCH_MSG_LENGTHS[msg_type];
}

/* Nanosleep helper */
static void nsleep(uint64_t nanoseconds) {
    struct timespec ts;
    ts.tv_sec = nanoseconds / 1000000000ULL;
    ts.tv_nsec = nanoseconds % 1000000000ULL;
    nanosleep(&ts, NULL);
}

/* Broadcast message to all connected clients */
static ssize_t broadcast_message(const uint8_t *msg, size_t len) {
    pthread_mutex_lock(&clients_mutex);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) continue;
        
        ssize_t sent = send(clients[i].socket_fd, msg, len, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EPIPE || errno == ECONNRESET) {
                printf("Client %d disconnected\n", i);
                close(clients[i].socket_fd);
                clients[i].active = 0;
            } else {
                perror("send");
            }
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
    return len;
}

/* Replay ITCH file with timestamp-accurate streaming */
static int replay_itch_file(const char *filename, double speed_multiplier, int is_gzip) {
    void *fp = NULL;
    uint8_t buffer[BUFFER_SIZE];
    size_t buffer_used = 0;
    uint64_t prev_timestamp = 0;
    uint64_t messages_sent = 0;
    uint64_t total_bytes = 0;
    
    // Open file (gzip or raw)
    if (is_gzip) {
        fp = gzopen(filename, "rb");
        if (!fp) {
            fprintf(stderr, "Failed to open gzip file: %s\n", filename);
            return -1;
        }
    } else {
        fp = fopen(filename, "rb");
        if (!fp) {
            fprintf(stderr, "Failed to open file: %s\n", filename);
            return -1;
        }
    }
    
    printf("Starting replay: %s (speed: %.2fx)\n", filename, speed_multiplier);
    
    while (server_running) {
        // Read more data if buffer is low
        if (buffer_used < BUFFER_SIZE / 2) {
            ssize_t bytes_read;
            
            if (is_gzip) {
                bytes_read = gzread(fp, buffer + buffer_used, BUFFER_SIZE - buffer_used);
            } else {
                bytes_read = fread(buffer + buffer_used, 1, BUFFER_SIZE - buffer_used, fp);
            }
            
            if (bytes_read < 0) {
                fprintf(stderr, "Read error\n");
                break;
            }
            
            if (bytes_read == 0 && buffer_used == 0) {
                printf("End of file reached\n");
                break;
            }
            
            buffer_used += bytes_read;
        }
        
        // Need at least 1 byte for message type
        if (buffer_used < 1) break;
        
        uint8_t msg_type = buffer[0];
        size_t msg_len = get_message_length(msg_type);
        
        if (msg_len == 0) {
            fprintf(stderr, "Unknown message type: %c (0x%02X)\n", msg_type, msg_type);
            // Skip this byte and continue
            memmove(buffer, buffer + 1, buffer_used - 1);
            buffer_used--;
            continue;
        }
        
        // Wait for complete message
        if (buffer_used < msg_len) {
            // Need more data
            if (is_gzip) {
                ssize_t bytes_read = gzread(fp, buffer + buffer_used, BUFFER_SIZE - buffer_used);
                if (bytes_read <= 0) break;
                buffer_used += bytes_read;
            } else {
                ssize_t bytes_read = fread(buffer + buffer_used, 1, BUFFER_SIZE - buffer_used, fp);
                if (bytes_read <= 0) break;
                buffer_used += bytes_read;
            }
            
            if (buffer_used < msg_len) {
                fprintf(stderr, "Incomplete message at end of file\n");
                break;
            }
        }
        
        // Parse timestamp (offset 5, 6 bytes) if message has header
        uint64_t current_timestamp = 0;
        if (msg_len >= 11) {
            current_timestamp = read_timestamp(buffer + 5);
        }
        
        // Calculate sleep time based on timestamp delta
        if (prev_timestamp > 0 && current_timestamp > prev_timestamp && speed_multiplier > 0) {
            uint64_t delta_ns = current_timestamp - prev_timestamp;
            uint64_t sleep_ns = (uint64_t)(delta_ns / speed_multiplier);
            
            // Cap sleep to prevent huge delays (max 1 second)
            if (sleep_ns > 1000000000ULL) {
                sleep_ns = 1000000000ULL;
            }
            
            if (sleep_ns > 1000) {  // Only sleep if > 1 microsecond
                nsleep(sleep_ns);
            }
        }
        
        // Broadcast message to all clients
        broadcast_message(buffer, msg_len);
        
        messages_sent++;
        total_bytes += msg_len;
        prev_timestamp = current_timestamp;
        
        // Progress update every 100k messages
        if (messages_sent % 100000 == 0) {
            printf("Sent %lu messages (%.2f MB)\n", messages_sent, total_bytes / 1048576.0);
        }
        
        // Remove message from buffer
        memmove(buffer, buffer + msg_len, buffer_used - msg_len);
        buffer_used -= msg_len;
    }
    
    printf("Replay complete: %lu messages, %.2f MB\n", messages_sent, total_bytes / 1048576.0);
    
    if (is_gzip) {
        gzclose(fp);
    } else {
        fclose(fp);
    }
    
    return 0;
}

/* Accept client connections */
static void* accept_clients_thread(void *arg) {
    int server_fd = *(int*)arg;
    
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        
        // Find free slot
        pthread_mutex_lock(&clients_mutex);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                slot = i;
                break;
            }
        }
        
        if (slot >= 0) {
            clients[slot].socket_fd = client_fd;
            clients[slot].address = client_addr;
            clients[slot].active = 1;
            printf("Client %d connected from %s:%d\n", 
                   slot, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        } else {
            printf("Max clients reached, rejecting connection\n");
            close(client_fd);
        }
        
        pthread_mutex_unlock(&clients_mutex);
    }
    
    return NULL;
}

/* Main server function */
int main(int argc, char *argv[]) {
    server_config_t config = {
        .filename = NULL,
        .port = DEFAULT_PORT,
        .speed_multiplier = DEFAULT_SPEED,
        .is_gzip = 0,
    };
    
    // Parse arguments
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <itch_file> [port] [speed_multiplier]\n", argv[0]);
        fprintf(stderr, "Example: %s data/01302019.NASDAQ_ITCH50 9999 1.0\n", argv[0]);
        return 1;
    }
    
    config.filename = argv[1];
    
    if (argc >= 3) {
        config.port = atoi(argv[2]);
    }
    
    if (argc >= 4) {
        config.speed_multiplier = atof(argv[3]);
    }
    
    // Check if file is gzipped
    size_t len = strlen(config.filename);
    if (len > 3 && strcmp(config.filename + len - 3, ".gz") == 0) {
        config.is_gzip = 1;
    }
    
    printf("ITCH Replay Server\n");
    printf("  File: %s\n", config.filename);
    printf("  Port: %d\n", config.port);
    printf("  Speed: %.2fx\n", config.speed_multiplier);
    printf("  Format: %s\n", config.is_gzip ? "gzip" : "raw binary");
    printf("\n");
    
    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }
    
    // Bind to port
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(config.port),
    };
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    
    // Listen for connections
    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }
    
    printf("Listening on port %d...\n", config.port);
    printf("Waiting for clients (press Ctrl+C to stop)...\n\n");
    
    // Initialize client slots
    memset(clients, 0, sizeof(clients));
    
    // Start accept thread
    pthread_t accept_thread;
    pthread_create(&accept_thread, NULL, accept_clients_thread, &server_fd);
    
    // Wait a moment for clients to connect
    sleep(2);
    
    // Start replay
    replay_itch_file(config.filename, config.speed_multiplier, config.is_gzip);
    
    // Cleanup
    server_running = 0;
    pthread_join(accept_thread, NULL);
    
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            close(clients[i].socket_fd);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    close(server_fd);
    
    printf("Server shutdown complete\n");
    return 0;
}
