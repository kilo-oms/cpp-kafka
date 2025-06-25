# ---------- Builder stage ----------
FROM ubuntu:22.04 AS build-env

ENV DEBIAN_FRONTEND=noninteractive

# Cập nhật package lists và cài công cụ build cần thiết
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    make \
    cmake \
    g++ \
    pkg-config \
    libyaml-cpp-dev \
    ca-certificates \
    curl \
    git \
    wget \
    # Dependencies cho librdkafka build từ source
    libssl-dev \
    libsasl2-dev \
    libzstd-dev \
    liblz4-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Build librdkafka từ source để có latest version
ENV RDKAFKA_VERSION=v2.10.1
RUN cd /tmp && \
    wget https://github.com/confluentinc/librdkafka/archive/${RDKAFKA_VERSION}.tar.gz && \
    tar -xzf ${RDKAFKA_VERSION}.tar.gz && \
    cd librdkafka-${RDKAFKA_VERSION#v} && \
    ./configure \
        --prefix=/usr/local \
        --enable-ssl \
        --enable-sasl \
        --enable-lz4-ext \
        --enable-zstd \
        --disable-debug-symbols && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    rm -rf /tmp/librdkafka*

# Tải json.hpp (header-only)
RUN mkdir -p /usr/local/include/nlohmann && \
    curl -sSL https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp \
    -o /usr/local/include/nlohmann/json.hpp

# Build và cài đặt flatbuffers
RUN git clone --depth 1 https://github.com/google/flatbuffers.git /tmp/flatbuffers && \
    cd /tmp/flatbuffers && \
    cmake -B build -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release \
        -DFLATBUFFERS_BUILD_TESTS=OFF && \
    cmake --build build -j$(nproc) && \
    cmake --install build && \
    rm -rf /tmp/flatbuffers

# Set environment variables cho build
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
ENV LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Verify librdkafka installation
RUN pkg-config --exists rdkafka && \
    pkg-config --modversion rdkafka && \
    ldconfig -p | grep librdkafka

# Làm việc trong thư mục ứng dụng
WORKDIR /app

# Copy toàn bộ mã nguồn vào container
COPY . .

# Biên dịch binary với proper linking
RUN mkdir -p bin obj && \
    make clean && \
    make all

# Verify binary can find librdkafka
RUN ldd bin/market_depth_processor | grep rdkafka || echo "Warning: librdkafka not found in binary dependencies"

# ---------- Final stage ----------
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Cài runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libyaml-cpp0.7 \
    ca-certificates \
    # Runtime libraries cho librdkafka
    libssl3 \
    libsasl2-2 \
    libzstd1 \
    liblz4-1 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Copy librdkafka từ build stage
COPY --from=build-env /usr/local/lib/librdkafka* /usr/local/lib/
COPY --from=build-env /usr/local/include/librdkafka /usr/local/include/librdkafka

# Copy flatbuffers runtime (nếu cần)
COPY --from=build-env /usr/local/lib/libflatbuffers* /usr/local/lib/
COPY --from=build-env /usr/local/bin/flatc /usr/local/bin/

# Update library cache
RUN ldconfig

# Set environment variables
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
ENV LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Thiết lập thư mục làm việc
WORKDIR /app

# Copy binary từ stage build
COPY --from=build-env /app/bin/market_depth_processor /bin/market_depth_processor

# Copy các file cấu hình
COPY config ./config

# Verify libraries are available at runtime
RUN ldconfig -p | grep rdkafka && \
    ldd /bin/market_depth_processor | grep rdkafka

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD /bin/market_depth_processor --version 2>/dev/null || exit 1

# Create non-root user for security
RUN groupadd -r appgroup && useradd -r -g appgroup appuser
RUN chown -R appuser:appgroup /app
USER appuser

# Lệnh mặc định khi container khởi chạy
CMD ["/bin/market_depth_processor", "-c", "/app/config/config.yaml"]