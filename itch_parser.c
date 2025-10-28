#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

/* NASDAQ ITCH 5.0 Parser
 * Specification: https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf
 */

typedef struct {
    char messageType;        // 1 byte
    uint16_t stockLocate;    // 2 bytes
    uint16_t trackingNumber; // 2 bytes
    uint64_t timestamp;      // 6 bytes (nanoseconds since midnight)
} ITCHHeader;

/* Read big-endian integer */
static inline uint64_t read_be(const uint8_t *b, size_t nbytes) {
    uint64_t v = 0;
    for (size_t i = 0; i < nbytes; ++i) v = (v << 8) | b[i];
    return v;
}

/* Fast 6-byte timestamp parser */
static inline uint64_t parse_timestamp(const uint8_t *b) {
    uint64_t tmp;
    memcpy(&tmp, b, 8);
    tmp = __builtin_bswap64(tmp);
    return tmp >> 16;
}

static inline uint16_t read_u16(const uint8_t *b) {
    return (uint16_t)((b[0] << 8) | b[1]);
}

static inline uint32_t read_u32(const uint8_t *b) {
    return (uint32_t)((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
}

static inline uint64_t read_u64(const uint8_t *b) {
    return read_be(b, 8);
}

/* Copy ASCII and trim spaces */
static void read_ascii(const uint8_t *b, size_t len, char *out, size_t outlen) {
    size_t n = (len < outlen - 1) ? len : outlen - 1;
    memcpy(out, b, n);
    while (n > 0 && out[n-1] == ' ') n--;
    out[n] = '\0';
}

/* Parse common header */
static ITCHHeader parse_header(const uint8_t *msg) {
    ITCHHeader h;
    h.messageType = (char)msg[0];
    h.stockLocate = read_u16(msg + 1);
    h.trackingNumber = read_u16(msg + 3);
    h.timestamp = parse_timestamp(msg + 5);
    return h;
}

/* [S] System Event (12 bytes) */
static void parse_S(const uint8_t *msg, size_t len) {
    if (len < 12) return;
    ITCHHeader h = parse_header(msg);
    char eventCode = (char)msg[11];
    
    printf("[S] System Event\n");
    printf("  Timestamp: %" PRIu64 " ns\n", h.timestamp);
    printf("  Event Code: %c\n", eventCode);
}

/* [R] Stock Directory (39 bytes) */
static void parse_R(const uint8_t *msg, size_t len) {
    if (len < 39) return;
    ITCHHeader h = parse_header(msg);
    
    char stock[9];
    read_ascii(msg + 11, 8, stock, sizeof(stock));
    char marketCategory = (char)msg[19];
    char financialStatus = (char)msg[20];
    uint32_t roundLotSize = read_u32(msg + 21);
    char roundLotsOnly = (char)msg[25];
    char issueClass = (char)msg[26];
    char issueSubType[3];
    read_ascii(msg + 27, 2, issueSubType, sizeof(issueSubType));
    char authenticity = (char)msg[29];
    char shortSaleThresholdIndicator = (char)msg[30];
    char ipoFlag = (char)msg[31];
    char luldRefPriceTier = (char)msg[32];
    char etpFlag = (char)msg[33];
    uint32_t etpLeverageFactor = read_u32(msg + 34);
    char inverseIndicator = (char)msg[38];
    
    printf("[R] Stock Directory\n");
    printf("  Stock: %s\n", stock);
    printf("  Market Category: %c\n", marketCategory);
    printf("  Financial Status: %c\n", financialStatus);
    printf("  Round Lot Size: %u\n", roundLotSize);
}

/* [H] Stock Trading Action (25 bytes) */
static void parse_H(const uint8_t *msg, size_t len) {
    if (len < 25) return;
    ITCHHeader h = parse_header(msg);
    
    char stock[9];
    read_ascii(msg + 11, 8, stock, sizeof(stock));
    char tradingState = (char)msg[19];
    char reserved = (char)msg[20];
    char reason[5];
    read_ascii(msg + 21, 4, reason, sizeof(reason));
    
    printf("[H] Stock Trading Action\n");
    printf("  Stock: %s\n", stock);
    printf("  Trading State: %c\n", tradingState);
    printf("  Reason: %s\n", reason);
}

/* [A] Add Order (No MPID) (36 bytes) */
static void parse_A(const uint8_t *msg, size_t len) {
    if (len < 36) return;
    ITCHHeader h = parse_header(msg);
    
    uint64_t orderRefNum = read_u64(msg + 11);
    char buySellIndicator = (char)msg[19];
    uint32_t shares = read_u32(msg + 20);
    char stock[9];
    read_ascii(msg + 24, 8, stock, sizeof(stock));
    uint32_t price = read_u32(msg + 32);
    
    printf("[A] Add Order (No MPID)\n");
    printf("  Order Ref: %" PRIu64 "\n", orderRefNum);
    printf("  Side: %c\n", buySellIndicator);
    printf("  Shares: %u\n", shares);
    printf("  Stock: %s\n", stock);
    printf("  Price: %u (%.4f)\n", price, price / 10000.0);
}

/* [F] Add Order (MPID) (40 bytes) */
static void parse_F(const uint8_t *msg, size_t len) {
    if (len < 40) return;
    ITCHHeader h = parse_header(msg);
    
    uint64_t orderRefNum = read_u64(msg + 11);
    char buySellIndicator = (char)msg[19];
    uint32_t shares = read_u32(msg + 20);
    char stock[9];
    read_ascii(msg + 24, 8, stock, sizeof(stock));
    uint32_t price = read_u32(msg + 32);
    char attribution[5];
    read_ascii(msg + 36, 4, attribution, sizeof(attribution));
    
    printf("[F] Add Order (MPID)\n");
    printf("  Order Ref: %" PRIu64 "\n", orderRefNum);
    printf("  Side: %c\n", buySellIndicator);
    printf("  Shares: %u\n", shares);
    printf("  Stock: %s\n", stock);
    printf("  Price: %u (%.4f)\n", price, price / 10000.0);
    printf("  MPID: %s\n", attribution);
}

/* [E] Order Executed (31 bytes) */
static void parse_E(const uint8_t *msg, size_t len) {
    if (len < 31) return;
    ITCHHeader h = parse_header(msg);
    
    uint64_t orderRefNum = read_u64(msg + 11);
    uint32_t executedShares = read_u32(msg + 19);
    uint64_t matchNumber = read_u64(msg + 23);
    
    printf("[E] Order Executed\n");
    printf("  Order Ref: %" PRIu64 "\n", orderRefNum);
    printf("  Executed Shares: %u\n", executedShares);
    printf("  Match Number: %" PRIu64 "\n", matchNumber);
}

/* [C] Order Executed With Price (36 bytes) */
static void parse_C(const uint8_t *msg, size_t len) {
    if (len < 36) return;
    ITCHHeader h = parse_header(msg);
    
    uint64_t orderRefNum = read_u64(msg + 11);
    uint32_t executedShares = read_u32(msg + 19);
    uint64_t matchNumber = read_u64(msg + 23);
    char printable = (char)msg[31];
    uint32_t executionPrice = read_u32(msg + 32);
    
    printf("[C] Order Executed With Price\n");
    printf("  Order Ref: %" PRIu64 "\n", orderRefNum);
    printf("  Executed Shares: %u\n", executedShares);
    printf("  Match Number: %" PRIu64 "\n", matchNumber);
    printf("  Execution Price: %u (%.4f)\n", executionPrice, executionPrice / 10000.0);
}

/* [X] Order Cancel (23 bytes) */
static void parse_X(const uint8_t *msg, size_t len) {
    if (len < 23) return;
    ITCHHeader h = parse_header(msg);
    
    uint64_t orderRefNum = read_u64(msg + 11);
    uint32_t cancelledShares = read_u32(msg + 19);
    
    printf("[X] Order Cancel\n");
    printf("  Order Ref: %" PRIu64 "\n", orderRefNum);
    printf("  Cancelled Shares: %u\n", cancelledShares);
}

/* [D] Order Delete (19 bytes) */
static void parse_D(const uint8_t *msg, size_t len) {
    if (len < 19) return;
    ITCHHeader h = parse_header(msg);
    
    uint64_t orderRefNum = read_u64(msg + 11);
    
    printf("[D] Order Delete\n");
    printf("  Order Ref: %" PRIu64 "\n", orderRefNum);
}

/* [U] Order Replace (35 bytes) */
static void parse_U(const uint8_t *msg, size_t len) {
    if (len < 35) return;
    ITCHHeader h = parse_header(msg);
    
    uint64_t origOrderRefNum = read_u64(msg + 11);
    uint64_t newOrderRefNum = read_u64(msg + 19);
    uint32_t shares = read_u32(msg + 27);
    uint32_t price = read_u32(msg + 31);
    
    printf("[U] Order Replace\n");
    printf("  Orig Order Ref: %" PRIu64 " -> New: %" PRIu64 "\n", origOrderRefNum, newOrderRefNum);
    printf("  Shares: %u\n", shares);
    printf("  Price: %u (%.4f)\n", price, price / 10000.0);
}

/* [P] Trade (Non-Cross) (44 bytes) */
static void parse_P(const uint8_t *msg, size_t len) {
    if (len < 44) return;
    ITCHHeader h = parse_header(msg);
    
    uint64_t orderRefNum = read_u64(msg + 11);
    char buySellIndicator = (char)msg[19];
    uint32_t shares = read_u32(msg + 20);
    char stock[9];
    read_ascii(msg + 24, 8, stock, sizeof(stock));
    uint32_t price = read_u32(msg + 32);
    uint64_t matchNumber = read_u64(msg + 36);
    
    printf("[P] Trade (Non-Cross)\n");
    printf("  Order Ref: %" PRIu64 "\n", orderRefNum);
    printf("  Side: %c\n", buySellIndicator);
    printf("  Shares: %u\n", shares);
    printf("  Stock: %s\n", stock);
    printf("  Price: %u (%.4f)\n", price, price / 10000.0);
    printf("  Match Number: %" PRIu64 "\n", matchNumber);
}

/* [Q] Cross Trade (40 bytes) */
static void parse_Q(const uint8_t *msg, size_t len) {
    if (len < 40) return;
    ITCHHeader h = parse_header(msg);
    
    uint64_t shares = read_u64(msg + 11);
    char stock[9];
    read_ascii(msg + 19, 8, stock, sizeof(stock));
    uint32_t crossPrice = read_u32(msg + 27);
    uint64_t matchNumber = read_u64(msg + 31);
    char crossType = (char)msg[39];
    
    printf("[Q] Cross Trade\n");
    printf("  Shares: %" PRIu64 "\n", shares);
    printf("  Stock: %s\n", stock);
    printf("  Cross Price: %u (%.4f)\n", crossPrice, crossPrice / 10000.0);
    printf("  Match Number: %" PRIu64 "\n", matchNumber);
    printf("  Cross Type: %c\n", crossType);
}

/* [B] Broken Trade (19 bytes) */
static void parse_B(const uint8_t *msg, size_t len) {
    if (len < 19) return;
    ITCHHeader h = parse_header(msg);
    
    uint64_t matchNumber = read_u64(msg + 11);
    
    printf("[B] Broken Trade\n");
    printf("  Match Number: %" PRIu64 "\n", matchNumber);
}

/* Message length lookup */
static size_t message_length_by_type(unsigned char t) {
    switch (t) {
        case 'S': return 12;  // System Event
        case 'R': return 39;  // Stock Directory
        case 'H': return 25;  // Stock Trading Action
        case 'Y': return 20;  // Reg SHO Restriction
        case 'L': return 26;  // Market Participant Position
        case 'V': return 35;  // MWCB Decline Level
        case 'W': return 12;  // MWCB Status
        case 'K': return 28;  // IPO Quoting Period Update
        case 'A': return 36;  // Add Order (No MPID)
        case 'F': return 40;  // Add Order (MPID)
        case 'E': return 31;  // Order Executed
        case 'C': return 36;  // Order Executed With Price
        case 'X': return 23;  // Order Cancel
        case 'D': return 19;  // Order Delete
        case 'U': return 35;  // Order Replace
        case 'P': return 44;  // Trade (Non-Cross)
        case 'Q': return 40;  // Cross Trade
        case 'B': return 19;  // Broken Trade
        case 'I': return 50;  // NOII
        case 'N': return 20;  // RPII
        default: return 0;
    }
}

/* Main dispatcher */
void parse_itch_message(const uint8_t *msg, size_t len) {
    if (len == 0) return;
    char type = (char)msg[0];
    
    switch (type) {
        case 'S': parse_S(msg, len); break;
        case 'R': parse_R(msg, len); break;
        case 'H': parse_H(msg, len); break;
        case 'A': parse_A(msg, len); break;
        case 'F': parse_F(msg, len); break;
        case 'E': parse_E(msg, len); break;
        case 'C': parse_C(msg, len); break;
        case 'X': parse_X(msg, len); break;
        case 'D': parse_D(msg, len); break;
        case 'U': parse_U(msg, len); break;
        case 'P': parse_P(msg, len); break;
        case 'Q': parse_Q(msg, len); break;
        case 'B': parse_B(msg, len); break;
        default:
            printf("[?] Unknown ITCH message type: %c (0x%02X)\n", type, type);
    }
    printf("\n");
}

/* Get message length for stream parsing */
size_t get_itch_message_length(uint8_t msg_type) {
    return message_length_by_type(msg_type);
}

#ifdef TEST_PARSER
int main() {
    // Example ITCH System Event message
    uint8_t msgS[] = {0x53,0x00,0x01,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x01,0x4F};
    
    // Example Add Order
    uint8_t msgA[] = {
        0x41,0x00,0x01,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x01,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,  // order ref
        0x42,  // Buy
        0x00,0x00,0x00,0x64,  // 100 shares
        0x41,0x41,0x50,0x4C,0x20,0x20,0x20,0x20,  // "AAPL    "
        0x00,0x01,0x86,0xA0  // $100.00
    };
    
    printf("=== ITCH 5.0 Parser Test ===\n\n");
    parse_itch_message(msgS, sizeof(msgS));
    parse_itch_message(msgA, sizeof(msgA));
    
    return 0;
}
#endif
