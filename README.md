# CBOE Market Depth Processor

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Build Status](https://img.shields.io/badge/build-passing-green.svg)](https://github.com/equix-tech/market-depth-processor)

High-frequency market data processing system for CBOE Level 2 order book snapshots with Change Data Capture (CDC) capabilities.

## 🚀 Overview

The **Market Depth Processor** is designed to handle high-throughput market data streams from CBOE exchanges, processing L2 order book snapshots and generating real-time CDC events and multi-depth JSON snapshots for downstream consumers.

### Key Features

- **High Performance**: Targets 10,000+ messages/second across 200,000+ symbols
- **Multi-Depth Snapshots**: Configurable depth levels (5, 10, 25, 50)
- **Change Data Capture**: Real-time CDC events for order book changes
- **Sequential Processing**: Per-symbol sequential processing for data integrity
- **FlatBuffers Input**: Efficient binary message format support
- **JSON Output**: Standard JSON format for downstream consumers
- **Kafka Integration**: Native Kafka producer/consumer support
- **Production Ready**: Comprehensive logging, monitoring, and error handling

## 📋 System Requirements

### Hardware Requirements
- **CPU**: Modern x86_64 processor (Intel/AMD)
- **Memory**: Minimum 4GB RAM (8GB+ recommended for production)
- **Storage**: 10GB+ available disk space
- **Network**: High-bandwidth network connection for market data

### Software Dependencies
- **OS**: Linux (Ubuntu 20.04+, CentOS 8+, RHEL 8+)
- **Compiler**: GCC 9+ or Clang 10+ with C++17 support
- **CMake**: Version 3.14+
- **Kafka**: Apache Kafka 2.8+

### Runtime Dependencies
```bash
# Ubuntu/Debian
sudo apt-get install -y \
    librdkafka1 \
    libyaml-cpp0.6 \
    libspdlog1 \
    libflatbuffers1 \
    libboost-system1.74.0 \
    libboost-thread1.74.0

# CentOS/RHEL
sudo yum install -y \
    librdkafka \
    yaml-cpp \
    spdlog \
    flatbuffers \
    boost-system \
    boost-thread
```

## 🏗️ Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   CBOE L2       │    │  Market Depth    │    │   Downstream    │
│   Snapshots     │───▶│   Processor      │───▶│   Consumers     │
│  (FlatBuffers)  │    │                  │    │   (JSON)        │
└─────────────────┘    └──────────────────┘    └─────────────────┘
       │                         │                       │
       │                         │                       │
   Kafka Input              Order Book               Kafka Output
   (Binary)                 Management               (Multi-format)
                                 │
                                 ▼
                        ┌──────────────────┐
                        │   CDC Events     │
                        │   Generation     │
                        └──────────────────┘
```

### Core Components

1. **KafkaConsumer**: High-performance Kafka message consumption
2. **OrderBookManager**: Multi-symbol order book state management
3. **MessageFactory**: JSON message generation and formatting
4. **MarketDepthProcessor**: Main processing orchestration
5. **CDCGenerator**: Change detection and event generation

## 🚀 Quick Start

### 1. Clone and Build

```bash
# Clone repository
git clone https://github.com/equix-tech/market-depth-processor.git
cd market-depth-processor

# Build using CMake
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 2. Configuration

Copy and customize the configuration file:

```bash
cp config/config.yaml config/production.yaml
# Edit config/production.yaml with your Kafka brokers and settings
```

Key configuration sections:
- **kafka_cluster**: Broker addresses and connection settings
- **depth_config**: Order book depth levels and CDC settings
- **topic_config**: Output topic routing configuration

### 3. Run the Processor

```bash
# Basic run with default config
./bin/market_depth_processor

# With custom config
./bin/market_depth_processor -c config/production.yaml

# Verbose mode for debugging
./bin/market_depth_processor -v --stats-interval 10

# Limited runtime for testing
./bin/market_depth_processor --runtime 300 -d 5,10,25
```

## 🐳 Docker Deployment

### Build Docker Image

```bash
# Build production image
docker build -t market-depth-processor:latest .

# Build development image
docker build --target development -t market-depth-processor:dev .
```

### Run with Docker Compose

```bash
# Start Kafka cluster and processor
docker-compose up -d

# View logs
docker-compose logs -f market-depth-processor

# Scale horizontally
docker-compose up -d --scale market-depth-processor=3
```

### Production Deployment

```bash
# Run with mounted config and logs
docker run -d \
  --name market-depth-processor \
  -v /opt/market-depth/config:/app/config:ro \
  -v /opt/market-depth/logs:/app/logs \
  --restart unless-stopped \
  market-depth-processor:latest
```

## 📊 Input/Output Formats

### Input: FlatBuffers Schema

The processor consumes CBOE L2 snapshots in FlatBuffers format:

```flatbuffers
table OrderBookSnapshot {
  symbol: string;
  seq: ulong;
  buy_side: [OrderMsgLevel];
  sell_side: [OrderMsgLevel];
  recent_trade_price: ulong;
  recent_trade_qty: uint;
}
```

### Output: JSON Snapshots

Multi-depth snapshots are published in JSON format:

```json
{
  "symbol": "AAPL",
  "sequence": 12345,
  "timestamp": 1640995200000000,
  "timestamp_iso": "2022-01-01T00:00:00.000Z",
  "message_type": "snapshot",
  "depth": 5,
  "bids": [
    {
      "symbol": "AAPL",
      "side": "bid",
      "price": "150.2500",
      "quantity": "1000.00",
      "number_of_orders": 5,
      "exchanges": ["CBOE"]
    }
  ],
  "asks": [
    {
      "symbol": "AAPL", 
      "side": "ask",
      "price": "150.2600",
      "quantity": "800.00",
      "number_of_orders": 3,
      "exchanges": ["CBOE"]
    }
  ],
  "market_stats": {
    "total_bid_levels": 25,
    "total_ask_levels": 23,
    "spread": "0.0100",
    "mid_price": "150.2550"
  }
}
```

### Output: CDC Events

Change events are published for real-time order book updates:

```json
{
  "symbol": "AAPL",
  "sequence": 12346,
  "timestamp": 1640995200001000,
  "message_type": "cdc",
  "event_type": "level_modified",
  "side": "bid",
  "level": {
    "symbol": "AAPL",
    "side": "bid", 
    "price": "150.2500",
    "quantity": "1200.00",
    "number_of_orders": 6,
    "exchanges": ["CBOE"]
  }
}
```

## ⚡ Performance Optimization

### Compilation Flags

```bash
# Maximum optimization for production
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native -mtune=native" \
      ..
```

### Runtime Tuning

1. **CPU Affinity**: Bind to specific CPU cores
```yaml
performance:
  cpu_affinity: [2, 3, 4, 5]
  thread_priority: -5
```

2. **Memory Optimization**: Use huge pages
```yaml
performance:
  huge_pages: true
  numa_policy: "preferred"
```

3. **Kafka Tuning**: Optimize batch sizes
```yaml
kafka_cluster:
  batch_num_messages: 10000
  linger_ms: 5
  compression: "lz4"
```

### Monitoring Performance

```bash
# Real-time statistics
./market_depth_processor -v --stats-interval 1

# Performance profiling with perf
perf record -g ./market_depth_processor
perf report
```

## 📈 Monitoring and Observability

### Built-in Metrics

The processor provides comprehensive metrics:

- **Throughput**: Messages per second, processing latency
- **Quality**: Error rates, validation failures
- **Resource**: CPU/Memory usage, queue depths
- **Business**: Symbol counts, depth statistics

### Logging

Structured logging with configurable levels:

```yaml
global:
  log_level: info           # trace, debug, info, warn, error
  log_path: /var/log/market-depth
```

Log formats include:
- **Timestamp**: Microsecond precision
- **Thread ID**: Multi-threading visibility
- **Component**: Source component identification
- **Symbol**: Per-symbol context

### Health Checks

```bash
# Docker health check
HEALTHCHECK --interval=30s --timeout=10s \
  CMD /app/bin/market_depth_processor --help

# Application health endpoint  
curl http://localhost:8081/health
```

## 🔧 Configuration Reference

### Complete Configuration Options

See [config/config.yaml](config/config.yaml) for detailed configuration documentation.

Key sections:

| Section | Purpose | Critical Settings |
|---------|---------|------------------|
| `kafka_cluster` | Kafka connectivity | `bootstrap_servers`, `compression` |
| `depth_config` | Order book behavior | `levels`, `enable_cdc` |
| `json_config` | Output formatting | `price_decimals`, `exchange_name` |
| `performance` | Runtime optimization | `cpu_affinity`, `thread_priority` |
| `monitoring` | Observability | `enable_metrics`, `stats_interval_s` |

### Environment Variables

Override configuration via environment variables:

```bash
export KAFKA_BOOTSTRAP_SERVERS="kafka1:9092,kafka2:9092"
export LOG_LEVEL="debug"
export STATS_INTERVAL="10"
```

## 🚨 Troubleshooting

### Common Issues

**1. Kafka Connection Failed**
```bash
# Check Kafka connectivity
kafkacat -b localhost:9092 -L

# Verify topic exists
kafka-topics --bootstrap-server localhost:9092 --list
```

**2. High Memory Usage**
```bash
# Monitor memory usage
top -p $(pgrep market_depth_processor)

# Enable memory profiling
valgrind --tool=massif ./market_depth_processor
```

**3. Processing Latency**
```bash
# Enable performance logging
./market_depth_processor -v --enable-profiling

# Check system resources
iostat -x 1
```

### Debug Mode

```bash
# Full debug output
./market_depth_processor \
  -c config/debug.yaml \
  --verbose \
  --stats-interval 1 \
  --runtime 60
```

### Performance Analysis

```bash
# CPU profiling
perf record -g -F 997 ./market_depth_processor
perf report --stdio

# Memory profiling  
valgrind --tool=memcheck --leak-check=full ./market_depth_processor

# Network analysis
tcpdump -i any -w kafka.pcap port 9092
```

## 🤝 Development

### Building from Source

```bash
# Install development dependencies
sudo apt-get install -y \
    build-essential \
    cmake \
    libboost-all-dev \
    librdkafka-dev \
    libyaml-cpp-dev \
    libspdlog-dev \
    nlohmann-json3-dev \
    libflatbuffers-dev

# Clone and build
git clone https://github.com/equix-tech/market-depth-processor.git
cd market-depth-processor
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

### Code Style

```bash
# Format code
clang-format -i src/*.cpp include/*.hpp

# Static analysis
clang-tidy src/*.cpp -- -Iinclude
cppcheck --enable=all --std=c++17 src/
```

### Testing

```bash
# Unit tests
make test

# Integration tests
./tests/run_integration_tests.sh

# Performance tests
./tests/run_performance_tests.sh
```

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🏢 Support

**Equix Technologies Pty Ltd**
- **Email**: contact@equix.com.au
- **Website**: https://www.equix.com.au
- **Documentation**: https://docs.equix.com.au/market-depth-processor

For technical support, please create an issue in the GitHub repository or contact our support team.

---

*Built with ❤️ by Equix Technologies for high-frequency trading infrastructure*