# ---------- Builder stage ----------
FROM ubuntu:20.04 AS build-env

ENV DEBIAN_FRONTEND=noninteractive
# Cài công cụ build và thư viện cần thiết
RUN apt -o Acquire::AllowInsecureRepositories=true  update && apt install -y --no-install-recommends \
    build-essential \
    make \
    g++ \
    libyaml-cpp-dev \
    librdkafka-dev \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*


# Tải json.hpp (header-only)
RUN mkdir -p /usr/local/include/nlohmann \
    && curl -sSL https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp \
    -o /usr/local/include/nlohmann/json.hpp
RUN git clone https://github.com/google/flatbuffers.git && \
    cd flatbuffers/ && \
    cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release && \
    make

# Làm việc trong thư mục ứng dụng
WORKDIR /app

# Copy toàn bộ mã nguồn vào container
COPY . .

# Biên dịch binary
RUN mkdir -p bin obj && make clean && make all

# ---------- Final stage ----------
FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# Cài thư viện runtime cần thiết
RUN apt -o Acquire::AllowInsecureRepositories=true update && apt install -y --no-install-recommends \
    libyaml-cpp-dev \
    librdkafka-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*
RUN git clone https://github.com/google/flatbuffers.git && \
    cd flatbuffers/ && \
    cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release && \
    make

# Thiết lập thư mục làm việc
WORKDIR /app

# Copy binary từ stage build vào
COPY --from=build-env /app/bin/market_depth_processor /bin/market_depth_processor

# Copy các file cấu hình nếu cần
COPY config ./config

# Lệnh mặc định khi container khởi chạy
CMD ["/bin/market_depth_processor", "-c", "/app/config/config.yaml"]
