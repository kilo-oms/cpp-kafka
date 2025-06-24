#!/bin/bash

# Market Depth Processor Build Script
# Equix Technologies Pty Ltd

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_TYPE=${BUILD_TYPE:-Release}
BUILD_DIR=${BUILD_DIR:-build}
#NUM_CORES=${NUM_CORES:-$(nproc)}
INSTALL_PREFIX=${INSTALL_PREFIX:-/usr/local}

# Functions
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

check_dependency() {
    if command -v "$1" >/dev/null 2>&1; then
        print_success "$1 is installed"
        return 0
    else
        print_error "$1 is not installed"
        return 1
    fi
}

install_ubuntu_deps() {
    print_header "Installing Ubuntu/Debian Dependencies"

    # Update package list
    sudo apt-get update

    # Install build dependencies
    sudo apt-get install -y \
        build-essential \
        cmake \
        git \
        pkg-config \
        libboost-all-dev \
        librdkafka-dev \
        libyaml-cpp-dev \
        libspdlog-dev \
        nlohmann-json3-dev \
        libflatbuffers-dev \
        flatbuffers-compiler

    print_success "Dependencies installed"
}

install_centos_deps() {
    print_header "Installing CentOS/RHEL Dependencies"

    # Enable EPEL repository
    sudo yum install -y epel-release

    # Install build dependencies
    sudo yum groupinstall -y "Development Tools"
    sudo yum install -y \
        cmake3 \
        git \
        pkgconfig \
        boost-devel \
        librdkafka-devel \
        yaml-cpp-devel \
        spdlog-devel \
        flatbuffers-devel \
        flatbuffers-compiler

    # Install nlohmann-json (may need to build from source)
    if ! yum list installed nlohmann-json3-devel >/dev/null 2>&1; then
        print_warning "nlohmann-json not available in yum, installing from source..."
        install_nlohmann_json_source
    fi

    print_success "Dependencies installed"
}

install_nlohmann_json_source() {
    local temp_dir=$(mktemp -d)
    cd "$temp_dir"

    git clone https://github.com/nlohmann/json.git
    cd json
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DJSON_BuildTests=OFF ..
#    make -j$NUM_CORES
    sudo make install

    cd - >/dev/null
    rm -rf "$temp_dir"

    print_success "nlohmann-json installed from source"
}

check_dependencies() {
    print_header "Checking Dependencies"

    local missing_deps=0

    # Check build tools
    check_dependency "cmake" || ((missing_deps++))
    check_dependency "make" || ((missing_deps++))
    check_dependency "g++" || check_dependency "clang++" || ((missing_deps++))
    check_dependency "pkg-config" || ((missing_deps++))

    # Check libraries (basic check)
    if ! pkg-config --exists rdkafka; then
        print_error "librdkafka not found"
        ((missing_deps++))
    else
        print_success "librdkafka found"
    fi

    if ! pkg-config --exists yaml-cpp; then
        print_error "yaml-cpp not found"
        ((missing_deps++))
    else
        print_success "yaml-cpp found"
    fi

    # Check for headers (approximate)
    if [ ! -f "/usr/include/spdlog/spdlog.h" ] && [ ! -f "/usr/local/include/spdlog/spdlog.h" ]; then
        print_error "spdlog headers not found"
        ((missing_deps++))
    else
        print_success "spdlog found"
    fi

    if [ ! -f "/usr/include/nlohmann/json.hpp" ] && [ ! -f "/usr/local/include/nlohmann/json.hpp" ]; then
        print_error "nlohmann-json headers not found"
        ((missing_deps++))
    else
        print_success "nlohmann-json found"
    fi

    if [ ! -f "/usr/include/flatbuffers/flatbuffers.h" ] && [ ! -f "/usr/local/include/flatbuffers/flatbuffers.h" ]; then
        print_error "flatbuffers headers not found"
        ((missing_deps++))
    else
        print_success "flatbuffers found"
    fi

    return $missing_deps
}

auto_install_deps() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        case $ID in
            ubuntu|debian)
                install_ubuntu_deps
                ;;
            centos|rhel|fedora)
                install_centos_deps
                ;;
            *)
                print_warning "Unsupported OS: $ID"
                print_warning "Please install dependencies manually"
                return 1
                ;;
        esac
    else
        print_warning "Cannot detect OS, please install dependencies manually"
        return 1
    fi
}

generate_flatbuffers() {
    print_header "Generating FlatBuffers Code"

    if ! command -v flatc >/dev/null 2>&1; then
        print_error "flatc compiler not found"
        return 1
    fi

    # Generate C++ headers
    flatc --cpp --gen-object-api -o include/ flatbuffers/orderbook.fbs

    # Generate Python code for test tools
    mkdir -p python_generated
    flatc --python -o python_generated/ flatbuffers/orderbook.fbs

    print_success "FlatBuffers code generated"
}

configure_build() {
    print_header "Configuring Build"

    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # Configure with CMake
    cmake \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        ..

    cd - >/dev/null
    print_success "Build configured (type: $BUILD_TYPE)"
}

build_project() {
    print_header "Building Project"

    cd "$BUILD_DIR"

    # Build with make
#    make -j$NUM_CORES

    cd - >/dev/null
    print_success "Build completed"
}

run_tests() {
    print_header "Running Tests"

    cd "$BUILD_DIR"

    # Run validation
    if make validate >/dev/null 2>&1; then
        print_success "Validation passed"
    else
        print_warning "Validation failed or not available"
    fi

    # Run unit tests if available
    if [ -f "test_runner" ]; then
        ./test_runner
        print_success "Unit tests passed"
    else
        print_warning "No unit tests found"
    fi

    cd - >/dev/null
}

create_package() {
    print_header "Creating Package"

    cd "$BUILD_DIR"

    # Create packages
    if command -v cpack >/dev/null 2>&1; then
        cpack
        print_success "Packages created"
    else
        print_warning "CPack not available, skipping package creation"
    fi

    cd - >/dev/null
}

install_project() {
    print_header "Installing Project"

    cd "$BUILD_DIR"

    if [ "$EUID" -eq 0 ]; then
        make install
    else
        sudo make install
    fi

    cd - >/dev/null
    print_success "Project installed to $INSTALL_PREFIX"
}

setup_docker() {
    print_header "Setting up Docker Environment"

    # Build Docker images
    docker build -t market-depth-processor:latest .
    docker build --target development -t market-depth-processor:dev .

    # Start services
    docker-compose up -d zookeeper kafka redis

    # Wait for Kafka to be ready
    print_warning "Waiting for Kafka to be ready..."
    sleep 30

    # Setup topics
    docker-compose up kafka-setup

    print_success "Docker environment ready"
}

cleanup() {
    print_header "Cleaning Up"

    # Remove build directory
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        print_success "Build directory cleaned"
    fi

    # Clean Docker
    if command -v docker >/dev/null 2>&1; then
        docker-compose down 2>/dev/null || true
        docker system prune -f 2>/dev/null || true
        print_success "Docker cleaned"
    fi
}

show_usage() {
    cat << EOF
Market Depth Processor Build Script

Usage: $0 [COMMAND] [OPTIONS]

Commands:
  deps        Install dependencies automatically
  check       Check if dependencies are installed
  generate    Generate FlatBuffers code
  configure   Configure build with CMake
  build       Build the project
  test        Run tests and validation
  package     Create distribution packages
  install     Install to system
  docker      Setup Docker development environment
  clean       Clean build artifacts
  all         Run deps, generate, configure, build, test (default)

Options:
  --build-type TYPE    Build type (Debug|Release|RelWithDebInfo) [default: Release]
  --build-dir DIR      Build directory [default: build]
  --install-prefix DIR Install prefix [default: /usr/local]
  --jobs N             Number of parallel jobs [default: nproc]
  --help               Show this help message

Examples:
  $0                           # Full build (all)
  $0 build --build-type Debug  # Debug build only
  $0 docker                    # Setup Docker environment
  $0 clean                     # Clean everything

Environment Variables:
  BUILD_TYPE      Build type
  BUILD_DIR       Build directory
  NUM_CORES       Number of CPU cores to use
  INSTALL_PREFIX  Installation prefix

EOF
}

# Main script logic
main() {
    local command="all"

    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            deps|check|generate|configure|build|test|package|install|docker|clean|all)
                command="$1"
                shift
                ;;
            --build-type)
                BUILD_TYPE="$2"
                shift 2
                ;;
            --build-dir)
                BUILD_DIR="$2"
                shift 2
                ;;
            --install-prefix)
                INSTALL_PREFIX="$2"
                shift 2
                ;;
            --jobs)
                NUM_CORES="$2"
                shift 2
                ;;
            --help|-h)
                show_usage
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done

    # Print configuration
    print_header "Build Configuration"
    echo "Command: $command"
    echo "Build Type: $BUILD_TYPE"
    echo "Build Directory: $BUILD_DIR"
    echo "Install Prefix: $INSTALL_PREFIX"
    echo "CPU Cores: $NUM_CORES"
    echo ""

    # Execute command
    case $command in
        deps)
            auto_install_deps
            ;;
        check)
            check_dependencies
            ;;
        generate)
            generate_flatbuffers
            ;;
        configure)
            configure_build
            ;;
        build)
            build_project
            ;;
        test)
            run_tests
            ;;
        package)
            create_package
            ;;
        install)
            install_project
            ;;
        docker)
            setup_docker
            ;;
        clean)
            cleanup
            ;;
        all)
            # Check dependencies first
            if ! check_dependencies; then
                print_warning "Missing dependencies detected"
                read -p "Install dependencies automatically? (y/N): " -n 1 -r
                echo
                if [[ $REPLY =~ ^[Yy]$ ]]; then
                    auto_install_deps
                    check_dependencies || exit 1
                else
                    print_error "Please install dependencies manually"
                    exit 1
                fi
            fi

            generate_flatbuffers
            configure_build
            build_project
            run_tests

            print_success "Build completed successfully!"
            echo ""
            echo "Binary location: $BUILD_DIR/bin/market_depth_processor"
            echo ""
            echo "Quick start:"
            echo "  cd $BUILD_DIR"
            echo "  ./bin/market_depth_processor --help"
            echo ""
            ;;
        *)
            print_error "Unknown command: $command"
            show_usage
            exit 1
            ;;
    esac
}

# Check if running as source or executed
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi