#!/bin/bash

# Fix Dependencies Script for Market Depth Processor
# Handles missing nlohmann-json and flatbuffers dependencies

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_header() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

detect_os() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo $ID
    elif [ -f /etc/redhat-release ]; then
        echo "centos"
    elif [ "$(uname)" = "Darwin" ]; then
        echo "macos"
    else
        echo "unknown"
    fi
}

install_ubuntu_dependencies() {
    print_header "Installing Ubuntu/Debian Dependencies"

    # Update package list
    sudo apt-get update

    # Install core build dependencies
    sudo apt-get install -y \
        build-essential \
        cmake \
        git \
        pkg-config \
        wget \
        curl

    # Install Boost
    sudo apt-get install -y libboost-all-dev

    # Install Kafka
    sudo apt-get install -y librdkafka-dev

    # Install YAML-cpp
    sudo apt-get install -y libyaml-cpp-dev

    # Try to install nlohmann-json from package manager
    if sudo apt-get install -y nlohmann-json3-dev; then
        print_success "nlohmann-json installed from package manager"
    else
        print_warning "nlohmann-json not available in package manager, will install from source"
        install_nlohmann_json_source
    fi

    # Try to install flatbuffers from package manager
    if sudo apt-get install -y libflatbuffers-dev flatbuffers-compiler; then
        print_success "flatbuffers installed from package manager"
    else
        print_warning "flatbuffers not available in package manager, will install from source"
        install_flatbuffers_source
    fi

    # Try to install spdlog from package manager
    if sudo apt-get install -y libspdlog-dev; then
        print_success "spdlog installed from package manager"
    else
        print_warning "spdlog not available in package manager, will install from source"
        install_spdlog_source
    fi
}

install_centos_dependencies() {
    print_header "Installing CentOS/RHEL Dependencies"

    # Enable EPEL
    sudo yum install -y epel-release

    # Install development tools
    sudo yum groupinstall -y "Development Tools"
    sudo yum install -y cmake3 git pkgconfig wget curl

    # Create symlink for cmake if needed
    if [ -f /usr/bin/cmake3 ] && [ ! -f /usr/bin/cmake ]; then
        sudo ln -sf /usr/bin/cmake3 /usr/bin/cmake
    fi

    # Install available packages
    sudo yum install -y boost-devel librdkafka-devel yaml-cpp-devel

    # Install from source (not available in standard repos)
    install_nlohmann_json_source
    install_flatbuffers_source
    install_spdlog_source
}

install_macos_dependencies() {
    print_header "Installing macOS Dependencies with Homebrew"

    # Check if Homebrew is installed
    if ! command -v brew >/dev/null 2>&1; then
        print_error "Homebrew not found. Please install Homebrew first:"
        echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi

    # Update Homebrew
    brew update

    # Install dependencies
    brew install cmake boost librdkafka yaml-cpp nlohmann-json flatbuffers spdlog
}

install_nlohmann_json_source() {
    print_header "Installing nlohmann-json from source"

    local temp_dir=$(mktemp -d)
    cd "$temp_dir"

    # Download latest release
    git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git
    cd json

    # Build and install
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DJSON_BuildTests=OFF \
          -DJSON_Install=ON \
          ..
    make -j$(nproc)
    sudo make install

    # Clean up
    cd /
    rm -rf "$temp_dir"

    print_success "nlohmann-json installed from source"
}

install_flatbuffers_source() {
    print_header "Installing FlatBuffers from source"

    local temp_dir=$(mktemp -d)
    cd "$temp_dir"

    # Download latest release
    git clone --depth 1 --branch v24.3.25 https://github.com/google/flatbuffers.git
    cd flatbuffers

    # Build and install
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DFLATBUFFERS_BUILD_TESTS=OFF \
          -DFLATBUFFERS_INSTALL=ON \
          ..
    make -j$(nproc)
    sudo make install

    # Clean up
    cd /
    rm -rf "$temp_dir"

    print_success "FlatBuffers installed from source"
}

install_spdlog_source() {
    print_header "Installing spdlog from source"

    local temp_dir=$(mktemp -d)
    cd "$temp_dir"

    # Download latest release
    git clone --depth 1 --branch v1.13.0 https://github.com/gabime/spdlog.git
    cd spdlog

    # Build and install
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DSPDLOG_BUILD_TESTS=OFF \
          -DSPDLOG_INSTALL=ON \
          ..
    make -j$(nproc)
    sudo make install

    # Clean up
    cd /
    rm -rf "$temp_dir"

    print_success "spdlog installed from source"
}

update_ldconfig() {
    print_header "Updating library cache"

    # Update library cache
    sudo ldconfig

    # Update pkg-config path if needed
    if [ -d "/usr/local/lib/pkgconfig" ]; then
        export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
        echo 'export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"' >> ~/.bashrc
    fi

    print_success "Library cache updated"
}

verify_installation() {
    print_header "Verifying Installation"

    local all_good=true

    # Check nlohmann-json
    if [ -f "/usr/include/nlohmann/json.hpp" ] || [ -f "/usr/local/include/nlohmann/json.hpp" ]; then
        print_success "nlohmann-json headers found"
    else
        print_error "nlohmann-json headers still missing"
        all_good=false
    fi

    # Check flatbuffers
    if [ -f "/usr/include/flatbuffers/flatbuffers.h" ] || [ -f "/usr/local/include/flatbuffers/flatbuffers.h" ]; then
        print_success "flatbuffers headers found"
    else
        print_error "flatbuffers headers still missing"
        all_good=false
    fi

    # Check flatc compiler
    if command -v flatc >/dev/null 2>&1; then
        print_success "flatc compiler found: $(which flatc)"
    else
        print_error "flatc compiler not found"
        all_good=false
    fi

    # Check spdlog
    if [ -f "/usr/include/spdlog/spdlog.h" ] || [ -f "/usr/local/include/spdlog/spdlog.h" ]; then
        print_success "spdlog headers found"
    else
        print_error "spdlog headers still missing"
        all_good=false
    fi

    # Check pkg-config
    echo ""
    print_header "Package Config Status"

    pkg-config --exists rdkafka && print_success "rdkafka: $(pkg-config --modversion rdkafka)" || print_error "rdkafka not found via pkg-config"
    pkg-config --exists yaml-cpp && print_success "yaml-cpp: $(pkg-config --modversion yaml-cpp)" || print_error "yaml-cpp not found via pkg-config"

    if $all_good; then
        print_success "All dependencies verified!"
        return 0
    else
        print_error "Some dependencies are still missing"
        return 1
    fi
}

fix_cmake_build() {
    print_header "Fixing CMake Build Configuration"

    # Remove old build directory
    if [ -d "cmake-build-debug" ]; then
        rm -rf cmake-build-debug
        print_success "Removed old build directory"
    fi

    if [ -d "build" ]; then
        rm -rf build
        print_success "Removed old build directory"
    fi

    # Create new build directory
    mkdir -p build
    cd build

    # Configure with explicit paths
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="/usr/local;/usr" \
        -DCMAKE_FIND_ROOT_PATH="/usr/local;/usr" \
        -DPkgConfig_ROOT="/usr/local/lib/pkgconfig:/usr/lib/pkgconfig" \
        ..

    cd ..
    print_success "CMake configured with explicit dependency paths"
}

regenerate_flatbuffers() {
    print_header "Regenerating FlatBuffers Code"

    if command -v flatc >/dev/null 2>&1; then
        # Regenerate C++ headers
        flatc --cpp --gen-object-api -o include/ flatbuffers/orderbook.fbs
        print_success "FlatBuffers C++ headers regenerated"

        # Regenerate Python code for test tools
        mkdir -p python_generated
        flatc --python -o python_generated/ flatbuffers/orderbook.fbs
        print_success "FlatBuffers Python code regenerated"
    else
        print_error "flatc compiler not available for regeneration"
    fi
}

main() {
    print_header "Market Depth Processor - Dependency Fixer"

    # Detect OS
    local os=$(detect_os)
    print_warning "Detected OS: $os"

    # Install dependencies based on OS
    case $os in
        ubuntu|debian)
            install_ubuntu_dependencies
            ;;
        centos|rhel|fedora)
            install_centos_dependencies
            ;;
        macos)
            install_macos_dependencies
            ;;
        *)
            print_error "Unsupported OS: $os"
            print_warning "Please install dependencies manually:"
            echo "  - nlohmann-json (headers)"
            echo "  - flatbuffers (headers + compiler)"
            echo "  - spdlog (headers)"
            exit 1
            ;;
    esac

    # Update library cache
    if [ "$os" != "macos" ]; then
        update_ldconfig
    fi

    # Verify installation
    if verify_installation; then
        # Regenerate FlatBuffers code
        regenerate_flatbuffers

        # Fix CMake build
        fix_cmake_build

        print_success "Dependencies fixed! You can now build the project:"
        echo ""
        echo "  cd build"
        echo "  make -j\$(nproc)"
        echo ""
        echo "Or use the convenience wrapper:"
        echo "  make build"
    else
        print_error "Dependency installation failed"
        exit 1
    fi
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi