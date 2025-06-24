# Market Depth Processor - Complete System Overview

## 🏗️ System Architecture

### High-Level Data Flow

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   CBOE Market   │    │  Market Depth    │    │   Downstream    │
│   Data Feed     │───▶│   Processor      │───▶│   Consumers     │
│  (FlatBuffers)  │    │                  │    │     (JSON)      │
└─────────────────┘    └──────────────────┘    └─────────────────┘
         │                       │                       │
    Kafka Input              Processing               Kafka Output
  (Binary Snapshot)         (Multi-threaded)        (JSON Messages)
                                   │
                                   ▼
                          ┌──────────────────┐
                          │   Order Book     │
                          │   State + CDC    │
                          └──────────────────┘
```

### Core Components

#### 1. **Data Ingestion Layer**
- **KafkaConsumer.hpp/cpp**: High-performance Kafka message consumption
- **FlatBuffers Parser**: Efficient binary message deserialization
- **Message Validation**: Data integrity and format checking

#### 2. **Order Book Management**
- **OrderBookTypes.hpp**: Core data structures and enums
- **OrderBook.hpp/cpp**: Symbol-specific order book state management
- **OrderBookManager**: Multi-symbol coordination and threading

#### 3. **Change Detection Engine**
- **CDC Generation**: Real-time change event detection
- **Level Comparison**: Efficient price level diffing
- **Event Classification**: Add/Modify/Remove/Clear events

#### 4. **Message Factory**
- **MessageFactory.hpp/cpp**: JSON message generation
- **Multi-depth Support**: Configurable depth levels (5, 10, 25, 50)
- **Format Standardization**: Consistent JSON schema output

#### 5. **Data Publishing**
- **KafkaProducer.hpp/cpp**: High-throughput message publishing
- **MessageRouter**: Topic routing and partitioning logic
- **Output Formatting**: JSON serialization and compression

#### 6. **Processing Orchestration**
- **MarketDepthProcessor.hpp/cpp**: Main processing engine
- **Threading Model**: Per-symbol sequential processing
- **Performance Monitoring**: Real-time metrics and statistics

## 📊 Performance Characteristics

### Target Performance
- **Throughput**: 10,000+ messages/second
- **Symbols**: 200,000+ concurrent symbols
- **Latency**: < 1ms processing time per message
- **Memory**: < 2GB RAM for 50,000 active symbols

### Optimization Strategies
1. **Zero-Copy Operations**: Minimal memory allocation
2. **Lock-Free Algorithms**: Atomic operations where possible
3. **CPU Affinity**: Core binding for consistent performance
4. **Batch Processing**: Efficient Kafka producer batching
5. **Memory Pools**: Pre-allocated data structures

## 🔄 Data Format Specifications

### Input: FlatBuffers Schema
```
OrderBookSnapshot {
  symbol: string
  seq: ulong
  buy_side: [OrderMsgLevel]    // Bid levels (highest to lowest)
  sell_side: [OrderMsgLevel]   // Ask levels (lowest to highest)
  recent_trade_price: ulong
  recent_trade_qty: uint
}

OrderMsgLevel {
  price: ulong                 // Scaled integer (4 decimal places)
  orders: [OrderMsgOrder]      // Orders at this price level
}

OrderMsgOrder {
  id: ulong                    // Unique order identifier
  qty: uint                    // Order quantity
  side: Side                   // Buy/Sell
}
```

### Output: JSON Schema

#### Market Depth Snapshot
```json
{
  "symbol": "AAPL",
  "sequence": 12345,
  "timestamp_us": 1640995200000000,
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
      "price": "150.2500",
      "quantity": "1000.00",
      "number_of_orders": 5,
      "exchanges": ["CBOE"]
    }
  ],
  "market_stats": {
    "total_bid_levels": 25,
    "total_ask_levels": 23,
    "spread": "0.0100",
    "mid_price": "150.2550",
    "has_sufficient_depth": true
  }
}
```

#### CDC Event
```json
{
  "symbol": "AAPL",
  "sequence": 12346,
  "timestamp_us": 1640995200001000,
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

## 🏭 Deployment Architecture

### Production Deployment
```
┌─────────────────────────────────────────────────────────────┐
│                    Production Environment                    │
├─────────────────────────────────────────────────────────────┤
│                                                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │   Kafka     │  │   Kafka     │  │   Kafka     │        │
│  │ Broker 1    │  │ Broker 2    │  │ Broker 3    │        │
│  └─────────────┘  └─────────────┘  └─────────────┘        │
│         │                 │                 │             │
│         └─────────────────┼─────────────────┘             │
│                           │                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │   Market    │  │   Market    │  │   Market    │        │
│  │   Depth     │  │   Depth     │  │   Depth     │        │
│  │ Processor 1 │  │ Processor 2 │  │ Processor 3 │        │
│  └─────────────┘  └─────────────┘  └─────────────┘        │
│         │                 │                 │             │
│         └─────────────────┼─────────────────┘             │
│                           │                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │ Downstream  │  │ Downstream  │  │ Downstream  │        │
│  │ Consumer 1  │  │ Consumer 2  │  │ Consumer 3  │        │
│  └─────────────┘  └─────────────┘  └─────────────┘        │
│                                                            │
└─────────────────────────────────────────────────────────────┘
```

### Container Orchestration
- **Docker**: Containerized application deployment
- **Kubernetes**: Production orchestration (optional)
- **Docker Compose**: Development environment setup
- **Health Checks**: Automated failure detection and recovery

## 📁 Project Structure

```
market-depth-processor/
├── src/                          # C++ source files
│   ├── main.cpp                  # Application entry point
│   ├── KafkaConsumer.cpp         # Kafka consumption logic
│   ├── KafkaProducer.cpp         # Kafka publishing logic
│   ├── OrderBook.cpp             # Order book state management
│   ├── MessageFactory.cpp        # JSON message generation
│   └── MarketDepthProcessor.cpp  # Main processing engine
├── include/                      # C++ header files
│   ├── KafkaConsumer.hpp
│   ├── KafkaProducer.hpp
│   ├── KafkaPush.hpp            # Inline Kafka publishing
│   ├── OrderBookTypes.hpp        # Core data types
│   ├── OrderBook.hpp
│   ├── MessageFactory.hpp
│   ├── MarketDepthProcessor.hpp
│   └── orderbook_generated.h     # FlatBuffers generated code
├── flatbuffers/                  # FlatBuffers schema
│   └── orderbook.fbs            # Message format definition
├── config/                       # Configuration files
│   └── config.yaml              # Application configuration
├── tests/                        # Test scripts and data
│   ├── test_producer.py         # Sample data generator
│   └── integration_tests.sh     # Integration test suite
├── monitoring/                   # Monitoring configuration
│   ├── prometheus.yml           # Metrics collection
│   ├── grafana-dashboards/      # Performance dashboards
│   └── alerts.yml               # Alert rules
├── docker/                       # Docker configuration
│   ├── Dockerfile               # Multi-stage build
│   ├── docker-compose.yml       # Development environment
│   └── Dockerfile.generator     # Test data generator
├── docs/                         # Documentation
│   ├── README.md                # Project overview
│   ├── SYSTEM_OVERVIEW.md       # This file
│   ├── API_REFERENCE.md         # API documentation
│   └── DEPLOYMENT_GUIDE.md      # Deployment instructions
├── scripts/                      # Build and utility scripts
│   ├── build.sh                 # Master build script
│   ├── setup_dev_env.sh         # Development setup
│   └── deploy.sh                # Production deployment
├── CMakeLists.txt               # CMake build configuration
├── Makefile                     # Convenience wrapper
└── .gitignore                   # Git ignore rules
```

## 🔧 Build System

### CMake Configuration
- **Multi-stage Build**: Debug, Release, RelWithDebInfo
- **Dependency Management**: Automatic library detection
- **Cross-platform**: Linux, macOS support
- **Package Generation**: DEB, RPM packages
- **Testing Integration**: Unit and integration tests

### Build Targets
```bash
# Full build process
make all                    # Configure, build, test

# Development workflow
make configure             # CMake configuration
make build                 # Compile sources
make test                  # Run validation
make install               # System installation

# Docker workflow
make docker                # Setup development environment
make docker-build          # Build container images
make docker-up             # Start services

# Quality assurance
make format                # Code formatting
make lint                  # Static analysis
make docs                  # Generate documentation
```

## 🚀 Quick Start Guide

### 1. System Setup
```bash
# Clone repository
git clone https://github.com/equix-tech/market-depth-processor.git
cd market-depth-processor

# Install dependencies and build
chmod +x build.sh
./build.sh all

# Or use Make wrapper
make all
```

### 2. Configuration
```bash
# Copy and customize configuration
cp config/config.yaml config/production.yaml

# Key settings to review:
# - kafka_cluster.bootstrap_servers
# - depth_config.levels
# - topic_config.snapshot_prefix
```

### 3. Running
```bash
# Basic run
./build/bin/market_depth_processor

# With custom config
./build/bin/market_depth_processor -c config/production.yaml

# Verbose mode with statistics
./build/bin/market_depth_processor -v --stats-interval 10
```

### 4. Docker Deployment
```bash
# Start development environment
make docker-up

# Run processor in container
docker-compose up market-depth-processor

# Scale horizontally
docker-compose up --scale market-depth-processor=3
```

## 📈 Monitoring and Observability

### Built-in Metrics
- **Throughput**: Messages/second, processing latency
- **Quality**: Error rates, validation failures
- **Resource**: CPU/Memory usage, queue depths
- **Business**: Symbol counts, depth statistics

### Logging Framework
- **Structured Logging**: JSON format with context
- **Log Levels**: Trace, Debug, Info, Warn, Error, Critical
- **Rotation**: Size and time-based log rotation
- **Performance**: High-speed logging with minimal overhead

### Health Monitoring
- **Application Health**: HTTP health check endpoint
- **Kafka Health**: Producer/consumer connection status
- **Order Book Health**: Symbol processing status
- **System Health**: Resource utilization monitoring

## 🔒 Security Considerations

### Kafka Security
- **SSL/TLS**: Encrypted broker connections
- **SASL**: Authentication mechanisms (PLAIN, SCRAM-SHA-256)
- **ACLs**: Topic-level access control
- **Network Isolation**: Private network deployment

### Application Security
- **Input Validation**: FlatBuffers message validation
- **Resource Limits**: Memory and CPU bounds
- **Error Handling**: Secure error message handling
- **Logging**: No sensitive data in logs

## 🔄 Maintenance and Operations

### Backup Strategy
- **Configuration**: Regular config backups
- **State**: Order book state snapshots (if needed)
- **Logs**: Centralized log aggregation
- **Metrics**: Historical metrics retention

### Update Procedures
1. **Blue/Green Deployment**: Zero-downtime updates
2. **Configuration Validation**: Pre-deployment testing
3. **Rollback Plan**: Quick reversion capability
4. **Health Verification**: Post-deployment validation

### Troubleshooting
- **Debug Mode**: Verbose logging and tracing
- **Performance Profiling**: Built-in profiling tools
- **Memory Analysis**: Valgrind integration
- **Network Analysis**: Kafka connection debugging

## 📋 Compliance and Standards

### Code Quality
- **C++17 Standard**: Modern C++ practices
- **Code Style**: Consistent formatting (clang-format)
- **Static Analysis**: Automated code quality checks
- **Documentation**: Comprehensive inline documentation

### Performance Standards
- **Latency SLA**: 99th percentile < 1ms
- **Throughput SLA**: 10,000+ msg/s sustained
- **Availability SLA**: 99.9% uptime
- **Resource SLA**: < 2GB RAM usage

### Operational Standards
- **Monitoring**: 24/7 system monitoring
- **Alerting**: Proactive issue notification
- **Documentation**: Up-to-date operational guides
- **Testing**: Comprehensive test coverage

---

This market depth processor represents a production-ready, high-frequency trading infrastructure component capable of handling CBOE market data at scale with real-time CDC capabilities and multi-format output support.