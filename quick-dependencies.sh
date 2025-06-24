#!/bin/bash

# Quick Dependency Fix for Market Depth Processor
# This script provides immediate solutions for common dependency issues

set -e

echo "🔧 Quick Dependency Fix for Market Depth Processor"
echo "=================================================="
echo ""

# Check if we're on a supported system
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Ubuntu/Debian quick fix
    if command -v apt-get >/dev/null 2>&1; then
        echo "📦 Installing missing dependencies on Ubuntu/Debian..."

        sudo apt-get update

        # Install nlohmann-json
        if ! dpkg -l | grep -q nlohmann-json3-dev; then
            echo "Installing nlohmann-json..."
            sudo apt-get install -y nlohmann-json3-dev || {
                echo "Package manager install failed, installing from source..."
                install_nlohmann_json_manual
            }
        fi

        # Install flatbuffers
        if ! dpkg -l | grep -q libflatbuffers-dev; then
            echo "Installing flatbuffers..."
            sudo apt-get install -y libflatbuffers-dev flatbuffers-compiler || {
                echo "Package manager install failed, installing from source..."
                install_flatbuffers_manual
            }
        fi

        # Install other missing deps
        sudo apt-get install -y libspdlog-dev librdkafka-dev libyaml-cpp-dev libboost-all-dev

    # CentOS/RHEL quick fix
    elif command -v yum >/dev/null 2>&1; then
        echo "📦 Installing dependencies on CentOS/RHEL..."

        sudo yum install -y epel-release
        sudo yum groupinstall -y "Development Tools"
        sudo yum install -y cmake3 boost-devel librdkafka-devel yaml-cpp-devel

        # These usually need to be installed from source
        install_nlohmann_json_manual
        install_flatbuffers_manual
        install_spdlog_manual

    else
        echo "❌ Unsupported Linux distribution"
        exit 1
    fi

elif [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    echo "📦 Installing dependencies on macOS..."

    if ! command -v brew >/dev/null 2>&1; then
        echo "❌ Homebrew not found. Please install it first:"
        echo "   /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi

    brew install nlohmann-json flatbuffers spdlog librdkafka yaml-cpp boost cmake

else
    echo "❌ Unsupported operating system"
    exit 1
fi

echo ""
echo "✅ Dependencies installed!"
echo ""

# Function to install nlohmann-json from source
install_nlohmann_json_manual() {
    echo "Installing nlohmann-json from source..."
    cd /tmp
    git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git
    cd json
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DJSON_BuildTests=OFF ..
    make -j$(nproc)
    sudo make install
    cd /tmp && rm -rf json
}

# Function to install flatbuffers from source
install_flatbuffers_manual() {
    echo "Installing flatbuffers from source..."
    cd /tmp
    git clone --depth 1 --branch v24.3.25 https://github.com/google/flatbuffers.git
    cd flatbuffers
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DFLATBUFFERS_BUILD_TESTS=OFF ..
    make -j$(nproc)
    sudo make install
    cd /tmp && rm -rf flatbuffers
}

# Function to install spdlog from source
install_spdlog_manual() {
    echo "Installing spdlog from source..."
    cd /tmp
    git clone --depth 1 --branch v1.13.0 https://github.com/gabime/spdlog.git
    cd spdlog
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DSPDLOG_BUILD_TESTS=OFF ..
    make -j$(nproc)
    sudo make install
    cd /tmp && rm -rf spdlog
}

# Update library cache
echo "🔄 Updating library cache..."
sudo ldconfig 2>/dev/null || true

# Clean and reconfigure build
echo "🧹 Cleaning old build files..."
rm -rf build cmake-build-debug

echo "⚙️  Configuring new build..."
mkdir -p build
cd build

# Configure with explicit paths
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="/usr/local;/usr" \
    ..

echo ""
echo "🎉 Quick fix complete!"
echo ""
echo "Now you can build the project:"
echo "  cd build"
echo "  make -j\$(nproc)"
echo ""
echo "Or test dependencies first:"
echo "  g++ -o check_deps ../check_deps.cpp -std=c++17 -lspdlog -lflatbuffers -lrdkafka -lyaml-cpp"
echo "  ./check_deps"