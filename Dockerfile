# Multi-stage Dockerfile for Market Depth Processor
# Stage 1: Build environment with all dependencies
FROM ubuntu:22.04 AS builder

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    pkg-config \
    libboost-all-dev \
    librdkafka-dev \
    libyaml-cpp-dev \
    libspdlog-dev \
    nlohmann-json3-dev \
    libflatbuffers-dev \
    flatbuffers-compiler \
    && rm -rf /var/lib/apt/lists/*

# Create working directory
WORKDIR /app

# Copy source code
COPY . .

# Create build directory and configure
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr/local \
          -DCMAKE_CXX_FLAGS="-O3 -DNDEBUG -march=x86-64 -mtune=generic" \
          ..

# Build the application
RUN cd build && make -j$(nproc)

# Validate the build
RUN cd build && make validate

# Stage 2: Runtime environment with minimal dependencies
FROM ubuntu:22.04 AS runtime

# Install only runtime dependencies
RUN apt-get update && apt-get install -y \
    librdkafka1 \
    libyaml-cpp0.6 \
    libspdlog1 \
    libflatbuffers1 \
    libboost-system1.74.0 \
    libboost-thread1.74.0 \
    libboost-filesystem1.74.0 \
    && rm -rf /var/lib/apt/lists/*

# Create application user for security
RUN groupadd -r marketdepth && useradd -r -g marketdepth -d /app -s /bin/bash marketdepth

# Create application directories
RUN mkdir -p /app/bin /app/config /app/logs /app/data && \
    chown -R marketdepth:marketdepth /app

# Copy binary and configuration from builder stage
COPY --from=builder /app/build/bin/market_depth_processor /app/bin/
COPY --from=builder /app/config/ /app/config/
COPY --from=builder /app/flatbuffers/ /app/flatbuffers/

# Set ownership
RUN chown -R marketdepth:marketdepth /app

# Switch to application user
USER marketdepth

# Set working directory
WORKDIR /app

# Create volume mount points
VOLUME ["/app/config", "/app/logs", "/app/data"]

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD /app/bin/market_depth_processor --help > /dev/null || exit 1

# Default command
CMD ["/app/bin/market_depth_processor", "-c", "/app/config/config.yaml"]

# Metadata labels
LABEL maintainer="Equix Technologies Pty Ltd <contact@equix.com.au>"
LABEL version="1.0.0"
LABEL description="High-frequency CBOE market depth processor"
LABEL org.opencontainers.image.title="Market Depth Processor"
LABEL org.opencontainers.image.description="Processes CBOE L2 market depth snapshots with CDC generation"
LABEL org.opencontainers.image.vendor="Equix Technologies Pty Ltd"
LABEL org.opencontainers.image.version="1.0.0"
LABEL org.opencontainers.image.source="https://github.com/equix-tech/market-depth-processor"

# Stage 3: Development environment (optional)
FROM builder AS development

# Install additional development tools
RUN apt-get update && apt-get install -y \
    gdb \
    valgrind \
    clang-format \
    clang-tidy \
    cppcheck \
    doxygen \
    graphviz \
    htop \
    vim \
    && rm -rf /var/lib/apt/lists/*

# Install performance profiling tools
RUN apt-get update && apt-get install -y \
    perf-tools-unstable \
    linux-tools-generic \
    && rm -rf /var/lib/apt/lists/*

# Create development user
RUN groupadd -r developer && useradd -r -g developer -d /workspace -s /bin/bash developer && \
    mkdir -p /workspace && chown -R developer:developer /workspace

# Switch to development user
USER developer
WORKDIR /workspace

# Development command
CMD ["/bin/bash"]

# Stage 4: Testing environment
FROM runtime AS testing

# Switch back to root for package installation
USER root

# Install testing dependencies
RUN apt-get update && apt-get install -y \
    curl \
    netcat \
    kafkacat \
    jq \
    && rm -rf /var/lib/apt/lists/*

# Copy test scripts and configurations
COPY tests/ /app/tests/
RUN chown -R marketdepth:marketdepth /app/tests/

# Switch back to application user
USER marketdepth

# Testing command
CMD ["/app/tests/run_tests.sh"]