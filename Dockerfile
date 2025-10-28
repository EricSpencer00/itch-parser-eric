FROM gcc:latest

# Install dependencies
RUN apt-get update && apt-get install -y \
    make \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source files
COPY itch_replay_server.c .
COPY Makefile .

# Build the server
RUN make itch_replay_server

# Create data directory
RUN mkdir -p /data

# Expose port
EXPOSE 9999

# Default command
CMD ["./itch_replay_server", "/data/sample.itch", "9999", "1.0"]
