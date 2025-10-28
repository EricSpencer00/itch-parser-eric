/*
 * Generate sample ITCH data for testing
 * Creates a small binary file with various ITCH message types
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static inline void write_u16(uint8_t *b, uint16_t v) {
    b[0] = (v >> 8) & 0xFF;
    b[1] = v & 0xFF;
}

static inline void write_u32(uint8_t *b, uint32_t v) {
    b[0] = (v >> 24) & 0xFF;
    b[1] = (v >> 16) & 0xFF;
    b[2] = (v >> 8) & 0xFF;
    b[3] = v & 0xFF;
}

static inline void write_u64(uint8_t *b, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        b[i] = (v >> (56 - i * 8)) & 0xFF;
    }
}

static inline void write_timestamp(uint8_t *b, uint64_t ts) {
    uint64_t tmp = ts << 16;
    tmp = __builtin_bswap64(tmp);
    memcpy(b, &tmp, 6);
}

static void write_stock(uint8_t *b, const char *stock) {
    memset(b, ' ', 8);
    size_t len = strlen(stock);
    if (len > 8) len = 8;
    memcpy(b, stock, len);
}

int main() {
    FILE *fp = fopen("data/sample.itch", "wb");
    if (!fp) {
        perror("fopen");
        return 1;
    }
    
    uint8_t msg[50];
    uint64_t ts = 34200000000000ULL;  // 9:30 AM in nanoseconds
    uint16_t stock_locate = 1;
    uint16_t tracking = 0;
    
    printf("Generating sample ITCH data...\n");
    
    // System Event - Start of Messages
    memset(msg, 0, sizeof(msg));
    msg[0] = 'S';
    write_u16(msg + 1, stock_locate);
    write_u16(msg + 3, tracking++);
    write_timestamp(msg + 5, ts);
    msg[11] = 'O';  // Start of Messages
    fwrite(msg, 12, 1, fp);
    ts += 1000000;
    
    // Stock Directory - AAPL
    memset(msg, 0, sizeof(msg));
    msg[0] = 'R';
    write_u16(msg + 1, stock_locate);
    write_u16(msg + 3, tracking++);
    write_timestamp(msg + 5, ts);
    write_stock(msg + 11, "AAPL");
    msg[19] = 'Q';  // NASDAQ
    msg[20] = 'N';  // Normal
    write_u32(msg + 21, 100);  // Round lot size
    msg[25] = 'Y';
    msg[26] = 'P';
    write_u16(msg + 27, 0);
    msg[29] = 'P';
    msg[30] = 'N';
    msg[31] = ' ';
    msg[32] = '1';
    msg[33] = 'N';
    write_u32(msg + 34, 1);
    msg[38] = 'N';
    fwrite(msg, 39, 1, fp);
    ts += 1000000;
    
    // Stock Directory - TSLA
    memset(msg, 0, sizeof(msg));
    msg[0] = 'R';
    write_u16(msg + 1, stock_locate + 1);
    write_u16(msg + 3, tracking++);
    write_timestamp(msg + 5, ts);
    write_stock(msg + 11, "TSLA");
    msg[19] = 'Q';
    msg[20] = 'N';
    write_u32(msg + 21, 100);
    msg[25] = 'Y';
    msg[26] = 'P';
    write_u16(msg + 27, 0);
    msg[29] = 'P';
    msg[30] = 'N';
    msg[31] = ' ';
    msg[32] = '1';
    msg[33] = 'N';
    write_u32(msg + 34, 1);
    msg[38] = 'N';
    fwrite(msg, 39, 1, fp);
    ts += 1000000;
    
    // Generate some trades
    for (int i = 0; i < 100; i++) {
        // Add Order - Buy AAPL
        memset(msg, 0, sizeof(msg));
        msg[0] = 'A';
        write_u16(msg + 1, stock_locate);
        write_u16(msg + 3, tracking++);
        write_timestamp(msg + 5, ts);
        write_u64(msg + 11, 1000000 + i);  // Order ref
        msg[19] = 'B';  // Buy
        write_u32(msg + 20, 100 + i * 10);  // Shares
        write_stock(msg + 24, "AAPL");
        write_u32(msg + 32, 1500000 + i * 100);  // Price ($150.00 + i * $0.01)
        fwrite(msg, 36, 1, fp);
        ts += 50000000;  // 50ms between orders
        
        // Add Order - Sell AAPL
        memset(msg, 0, sizeof(msg));
        msg[0] = 'A';
        write_u16(msg + 1, stock_locate);
        write_u16(msg + 3, tracking++);
        write_timestamp(msg + 5, ts);
        write_u64(msg + 11, 2000000 + i);  // Order ref
        msg[19] = 'S';  // Sell
        write_u32(msg + 20, 100 + i * 10);  // Shares
        write_stock(msg + 24, "AAPL");
        write_u32(msg + 32, 1500100 + i * 100);  // Price ($150.01 + i * $0.01)
        fwrite(msg, 36, 1, fp);
        ts += 50000000;
        
        // Execute some orders
        if (i % 5 == 0) {
            memset(msg, 0, sizeof(msg));
            msg[0] = 'E';
            write_u16(msg + 1, stock_locate);
            write_u16(msg + 3, tracking++);
            write_timestamp(msg + 5, ts);
            write_u64(msg + 11, 1000000 + i);
            write_u32(msg + 19, 50);  // Executed shares
            write_u64(msg + 23, 3000000 + i);  // Match number
            fwrite(msg, 31, 1, fp);
            ts += 10000000;
        }
    }
    
    // System Event - End of Messages
    memset(msg, 0, sizeof(msg));
    msg[0] = 'S';
    write_u16(msg + 1, stock_locate);
    write_u16(msg + 3, tracking++);
    write_timestamp(msg + 5, ts);
    msg[11] = 'C';  // End of Messages
    fwrite(msg, 12, 1, fp);
    
    fclose(fp);
    
    printf("Sample ITCH data written to data/sample.itch\n");
    printf("Messages generated: ~200\n");
    
    return 0;
}
