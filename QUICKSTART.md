# ITCH Replay System - Quick Reference

## 🚀 Quick Start (3 Steps)

```bash
# 1. Build everything
make clean && make

# 2. Generate sample data  
./generate_sample_itch

# 3a. Start server (Terminal 1)
./itch_replay_server data/sample.itch 9999 1.0

# 3b. Connect client (Terminal 2)
./itch_client 127.0.0.1 9999
```

## 📋 Common Commands

### Server Commands

```bash
# Real-time replay
./itch_replay_server data/file.itch 9999 1.0

# 10x speed (backtesting)
./itch_replay_server data/file.itch 9999 10.0

# Max speed (no delay)
./itch_replay_server data/file.itch 9999 0

# Gzip file
./itch_replay_server data/file.itch.gz 9999 1.0
```

### Client Commands

```bash
# Connect to local server
./itch_client 127.0.0.1 9999

# Connect to remote server
./itch_client 192.168.1.100 9999
```

### Docker Commands

```bash
# Build image
docker build -t itch-replay .

# Run container
docker run -p 9999:9999 -v $(pwd)/data:/data:ro itch-replay

# Docker Compose
docker-compose up
docker-compose down
```

## 📊 File Formats

### ITCH Binary File Structure

```
[Message 1: Type + Header + Payload]
[Message 2: Type + Header + Payload]
...
```

Each message:
- Fixed length per message type
- Big-endian integers
- 6-byte timestamps (nanoseconds since midnight)

### Message Lengths (ITCH 5.0)

| Type | Bytes | Description |
|------|-------|-------------|
| S | 12 | System Event |
| R | 39 | Stock Directory |
| A | 36 | Add Order (No MPID) |
| F | 40 | Add Order (MPID) |
| E | 31 | Order Executed |
| C | 36 | Order Executed w/ Price |
| X | 23 | Order Cancel |
| D | 19 | Order Delete |
| U | 35 | Order Replace |
| P | 44 | Trade (Non-Cross) |
| Q | 40 | Cross Trade |
| B | 19 | Broken Trade |

## 🎯 Use Cases

### 1. Backtesting Strategy (Real-Time Speed)
```bash
# Terminal 1
./itch_replay_server historical/20210130.itch 9999 1.0

# Terminal 2
./my_strategy 127.0.0.1 9999
```

### 2. Fast Backtesting (100x Speed)
```bash
./itch_replay_server historical/20210130.itch 9999 100.0
```

### 3. Order Book Reconstruction
```bash
# Modify client to maintain order book state
# Track: Add (A/F), Execute (E/C), Cancel (X), Delete (D)
```

### 4. Multi-Client Testing
```bash
# Terminal 1: Server
./itch_replay_server data/sample.itch 9999 1.0

# Terminal 2-N: Multiple clients
./itch_client 127.0.0.1 9999
./trading_bot_1 127.0.0.1 9999
./trading_bot_2 127.0.0.1 9999
```

## 🔧 Customization

### Modify Replay Speed
Edit server command line parameter 3:
- `0.0` = No delay (max speed)
- `0.1` = 10x faster
- `1.0` = Real-time
- `10.0` = 10x slower (for debugging)

### Change Buffer Size
Edit in source files:
```c
#define BUFFER_SIZE (64 * 1024)  // Increase for higher throughput
```

### Add Custom Parsing
Edit `itch_parser.c` to add message-specific logic:
```c
static void parse_A(const uint8_t *msg, size_t len) {
    // Your custom order book logic here
    update_order_book(orderRefNum, side, shares, stock, price);
}
```

## 🐛 Troubleshooting

### Server won't start
```bash
# Check if port is in use
lsof -i :9999

# Kill existing process
kill <PID>

# Use different port
./itch_replay_server data/sample.itch 10000 1.0
```

### Client can't connect
```bash
# Verify server is running
ps aux | grep itch_replay

# Check firewall
sudo ufw allow 9999/tcp

# Use IP instead of hostname
./itch_client 127.0.0.1 9999  # NOT localhost
```

### Gzip not working
```bash
# Install zlib development headers
# Ubuntu/Debian:
sudo apt-get install zlib1g-dev

# macOS:
brew install zlib

# Rebuild
make clean && make
```

## 📈 Performance Tuning

### For Maximum Throughput

```bash
# Server: Zero delay
./itch_replay_server data/huge.itch 9999 0

# Client: Increase buffer
# Edit BUFFER_SIZE in itch_client.c to (256 * 1024)
```

### For Accurate Timing

```bash
# Server: Real-time or slight speedup
./itch_replay_server data/file.itch 9999 1.0

# Use high-resolution timer in client
# Already implemented with clock_gettime(CLOCK_MONOTONIC)
```

## 📁 File Organization

```
c-lib/
├── itch_replay_server   # Server binary
├── itch_client          # Client binary
├── generate_sample_itch # Data generator
├── data/
│   ├── sample.itch      # Generated sample
│   └── *.itch.gz        # Historical data (add yours here)
├── Dockerfile
└── docker-compose.yml
```

## 🔗 Resources

- ITCH 5.0 Spec: https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf
- Historical Data: https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/
- ITTO Spec (Options): https://www.nasdaqtrader.com/content/productsservices/trading/optionsmarket/itto_spec40.pdf

## ⚡ Advanced

### Stream to Kafka
```c
// In server, after broadcast_message()
rdkafka_produce(topic, msg, len);
```

### WebSocket Bridge
```c
// Use libwebsockets to wrap TCP feed
ws_send_binary(client, msg, len);
```

### Redis Pub/Sub
```c
// Publish to Redis channel
redisCommand(ctx, "PUBLISH itch %b", msg, len);
```

---

**Questions? Check the main README.md for full documentation.**
