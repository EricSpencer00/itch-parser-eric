/*
 * ITCH Client - Connects to ITCH replay server and parses messages
 * 
 * Usage:
 *   ./itch_client [host] [port]
 * 
 * Example:
 *   ./itch_client localhost 9999
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define BUFFER_SIZE (64 * 1024)
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 9999

/* Import parser functions (declared in itch_parser.c) */
extern void parse_itch_message(const uint8_t *msg, size_t len);
extern size_t get_itch_message_length(uint8_t msg_type);

/* Statistics */
typedef struct {
    uint64_t total_messages;
    uint64_t total_bytes;
    uint64_t messages_by_type[256];
    struct timespec start_time;
    struct timespec last_update;
} stats_t;

static void init_stats(stats_t *stats) {
    memset(stats, 0, sizeof(stats_t));
    clock_gettime(CLOCK_MONOTONIC, &stats->start_time);
    stats->last_update = stats->start_time;
}

static void print_stats(stats_t *stats) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    double elapsed = (now.tv_sec - stats->start_time.tv_sec) + 
                     (now.tv_nsec - stats->start_time.tv_nsec) / 1e9;
    
    printf("\n=== Statistics ===\n");
    printf("Total Messages: %lu\n", stats->total_messages);
    printf("Total Bytes: %.2f MB\n", stats->total_bytes / 1048576.0);
    printf("Elapsed Time: %.2f seconds\n", elapsed);
    printf("Message Rate: %.0f msg/sec\n", stats->total_messages / elapsed);
    printf("Throughput: %.2f MB/sec\n", (stats->total_bytes / 1048576.0) / elapsed);
    
    printf("\nMessage Type Breakdown:\n");
    const char *type_names[] = {
        ['S'] = "System Event",
        ['R'] = "Stock Directory",
        ['H'] = "Trading Action",
        ['A'] = "Add Order (No MPID)",
        ['F'] = "Add Order (MPID)",
        ['E'] = "Order Executed",
        ['C'] = "Order Executed w/ Price",
        ['X'] = "Order Cancel",
        ['D'] = "Order Delete",
        ['U'] = "Order Replace",
        ['P'] = "Trade (Non-Cross)",
        ['Q'] = "Cross Trade",
        ['B'] = "Broken Trade",
    };
    
    for (int i = 0; i < 256; i++) {
        if (stats->messages_by_type[i] > 0) {
            const char *name = type_names[i] ? type_names[i] : "Unknown";
            printf("  [%c] %-25s : %10lu (%.1f%%)\n", 
                   (char)i, name, stats->messages_by_type[i],
                   100.0 * stats->messages_by_type[i] / stats->total_messages);
        }
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    
    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
    }
    
    printf("ITCH Client\n");
    printf("Connecting to %s:%d...\n", host, port);
    
    // Create socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return 1;
    }
    
    // Connect to server
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", host);
        close(sock_fd);
        return 1;
    }
    
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return 1;
    }
    
    printf("Connected!\n\n");
    
    // Initialize stats
    stats_t stats;
    init_stats(&stats);
    
    // Receive buffer
    uint8_t buffer[BUFFER_SIZE];
    size_t buffer_used = 0;
    
    int verbose = 0;  // Set to 1 to print all messages
    
    while (1) {
        // Read data from socket
        ssize_t bytes_read = recv(sock_fd, buffer + buffer_used, BUFFER_SIZE - buffer_used, 0);
        
        if (bytes_read < 0) {
            perror("recv");
            break;
        }
        
        if (bytes_read == 0) {
            printf("Server disconnected\n");
            break;
        }
        
        buffer_used += bytes_read;
        
        // Parse messages from buffer
        while (buffer_used > 0) {
            if (buffer_used < 1) break;
            
            uint8_t msg_type = buffer[0];
            size_t msg_len = get_itch_message_length(msg_type);
            
            if (msg_len == 0) {
                fprintf(stderr, "Unknown message type: %c (0x%02X)\n", msg_type, msg_type);
                // Skip byte
                memmove(buffer, buffer + 1, buffer_used - 1);
                buffer_used--;
                continue;
            }
            
            // Wait for complete message
            if (buffer_used < msg_len) {
                break;
            }
            
            // Parse message
            if (verbose) {
                parse_itch_message(buffer, msg_len);
            }
            
            // Update stats
            stats.total_messages++;
            stats.total_bytes += msg_len;
            stats.messages_by_type[msg_type]++;
            
            // Print progress every 100k messages
            if (stats.total_messages % 100000 == 0) {
                printf("Received %lu messages (%.2f MB)\n", 
                       stats.total_messages, stats.total_bytes / 1048576.0);
            }
            
            // Remove message from buffer
            memmove(buffer, buffer + msg_len, buffer_used - msg_len);
            buffer_used -= msg_len;
        }
        
        // Check if buffer is getting full
        if (buffer_used > BUFFER_SIZE * 0.9) {
            fprintf(stderr, "Warning: buffer nearly full, may be falling behind\n");
        }
    }
    
    // Print final stats
    print_stats(&stats);
    
    close(sock_fd);
    return 0;
}
