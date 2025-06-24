# Market Depth Processor - Convenience Makefile
# Equix Technologies Pty Ltd

# Configuration
BUILD_DIR ?= build
BUILD_TYPE ?= Release
NUM_CORES ?= $(shell nproc)

# Colors
BLUE := \033[0;34m
GREEN := \033[0;32m
YELLOW := \033[1;33m
RED := \033[0;31m
NC := \033[0m

.PHONY: help all deps check configure build test install clean docker run run-verbose run-test format lint docs

# Default target
all: configure build test

help: ## Show this help message
	@echo "$(BLUE)Market Depth Processor Build System$(NC)"
	@echo ""
	@echo "$(GREEN)Available targets:$(NC)"
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z_-]+:.*?## / {printf "  $(YELLOW)%-15s$(NC) %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@echo ""
	@echo "$(GREEN)Environment Variables:$(NC)"
	@echo "  BUILD_TYPE    Build type (Debug|Release|RelWithDebInfo) [$(BUILD_TYPE)]"
	@echo "  BUILD_DIR     Build directory [$(BUILD_DIR)]"
	@echo "  NUM_CORES     Number of parallel jobs [$(NUM_CORES)]"
	@echo ""
	@echo "$(GREEN)Examples:$(NC)"
	@echo "  make all BUILD_TYPE=Debug"
	@echo "  make docker"
	@echo "  make run-verbose"

deps: ## Install system dependencies
	@echo "$(BLUE)Installing dependencies...$(NC)"
	./build.sh deps

check: ## Check if dependencies are installed
	@echo "$(BLUE)Checking dependencies...$(NC)"
	./build.sh check

generate: ## Generate FlatBuffers code
	@echo "$(BLUE)Generating FlatBuffers code...$(NC)"
	./build.sh generate

configure: generate ## Configure build with CMake
	@echo "$(BLUE)Configuring build...$(NC)"
	./build.sh configure --build-type $(BUILD_TYPE) --build-dir $(BUILD_DIR)

build: configure ## Build the project
	@echo "$(BLUE)Building project...$(NC)"
	cd $(BUILD_DIR) && $(MAKE) -j$(NUM_CORES)

test: build ## Run tests and validation
	@echo "$(BLUE)Running tests...$(NC)"
	cd $(BUILD_DIR) && $(MAKE) validate || true

install: build ## Install to system
	@echo "$(BLUE)Installing project...$(NC)"
	cd $(BUILD_DIR) && sudo $(MAKE) install

package: build ## Create distribution packages
	@echo "$(BLUE)Creating packages...$(NC)"
	cd $(BUILD_DIR) && cpack

clean: ## Clean build artifacts
	@echo "$(BLUE)Cleaning build artifacts...$(NC)"
	rm -rf $(BUILD_DIR)
	docker-compose down 2>/dev/null || true

distclean: clean ## Clean everything including generated code
	@echo "$(BLUE)Deep cleaning...$(NC)"
	rm -rf $(BUILD_DIR) python_generated/
	rm -f include/orderbook_generated.h
	docker system prune -af 2>/dev/null || true

# Docker targets
docker: ## Setup Docker development environment
	@echo "$(BLUE)Setting up Docker environment...$(NC)"
	./build.sh docker

docker-build: ## Build Docker images
	@echo "$(BLUE)Building Docker images...$(NC)"
	docker build -t market-depth-processor:latest .
	docker build --target development -t market-depth-processor:dev .

docker-up: ## Start Docker services
	@echo "$(BLUE)Starting Docker services...$(NC)"
	docker-compose up -d

docker-down: ## Stop Docker services
	@echo "$(BLUE)Stopping Docker services...$(NC)"
	docker-compose down

docker-logs: ## Show Docker logs
	@echo "$(BLUE)Showing Docker logs...$(NC)"
	docker-compose logs -f market-depth-processor

# Run targets
run: build ## Run with default configuration
	@echo "$(BLUE)Running market depth processor...$(NC)"
	cd $(BUILD_DIR) && ./bin/market_depth_processor -c ../config/config.yaml

run-verbose: build ## Run in verbose mode
	@echo "$(BLUE)Running in verbose mode...$(NC)"
	cd $(BUILD_DIR) && ./bin/market_depth_processor -c ../config/config.yaml -v --stats-interval 10

run-test: build ## Run for 60 seconds in test mode
	@echo "$(BLUE)Running test mode (60 seconds)...$(NC)"
	cd $(BUILD_DIR) && ./bin/market_depth_processor -c ../config/config.yaml -v --runtime 60

run-debug: ## Run with GDB debugger
	@echo "$(BLUE)Running with debugger...$(NC)"
	cd $(BUILD_DIR) && gdb --args ./bin/market_depth_processor -c ../config/config.yaml -v

run-valgrind: build ## Run with Valgrind memory checker
	@echo "$(BLUE)Running with Valgrind...$(NC)"
	cd $(BUILD_DIR) && valgrind --tool=memcheck --leak-check=full ./bin/market_depth_processor -c ../config/config.yaml --runtime 30

# Development tools
format: ## Format code with clang-format
	@echo "$(BLUE)Formatting code...$(NC)"
	find src include -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i

lint: ## Run static analysis with clang-tidy
	@echo "$(BLUE)Running static analysis...$(NC)"
	cd $(BUILD_DIR) && clang-tidy ../src/*.cpp -- -I../include

cppcheck: ## Run cppcheck static analysis
	@echo "$(BLUE)Running cppcheck...$(NC)"
	cppcheck --enable=all --std=c++17 --inconclusive --xml --xml-version=2 src/ 2> cppcheck-report.xml || true

docs: ## Generate documentation with Doxygen
	@echo "$(BLUE)Generating documentation...$(NC)"
	cd $(BUILD_DIR) && $(MAKE) docs 2>/dev/null || echo "$(YELLOW)Doxygen not available$(NC)"

# Performance analysis
profile: build ## Run with performance profiling
	@echo "$(BLUE)Running performance profiling...$(NC)"
	cd $(BUILD_DIR) && perf record -g ./bin/market_depth_processor -c ../config/config.yaml --runtime 30
	cd $(BUILD_DIR) && perf report

benchmark: build ## Run performance benchmark
	@echo "$(BLUE)Running benchmark...$(NC)"
	cd $(BUILD_DIR) && time ./bin/market_depth_processor -c ../config/config.yaml --runtime 60 --stats-interval 1

# Testing targets
integration-test: docker-up ## Run integration tests
	@echo "$(BLUE)Running integration tests...$(NC)"
	sleep 10  # Wait for services to start
	python3 test_producer.py --bootstrap-servers localhost:9092 --rate 100 --runtime 10 &
	cd $(BUILD_DIR) && timeout 15 ./bin/market_depth_processor -c ../config/config.yaml -v || true
	docker-compose down

load-test: docker-up ## Run load test
	@echo "$(BLUE)Running load test...$(NC)"
	sleep 10
	python3 test_producer.py --bootstrap-servers localhost:9092 --rate 5000 --runtime 60 &
	cd $(BUILD_DIR) && timeout 65 ./bin/market_depth_processor -c ../config/config.yaml -v --stats-interval 5 || true
	docker-compose down

# Release targets
release: ## Build release version
	$(MAKE) all BUILD_TYPE=Release

debug: ## Build debug version
	$(MAKE) all BUILD_TYPE=Debug

release-package: release package ## Build release and create packages
	@echo "$(GREEN)Release package created in $(BUILD_DIR)/$(NC)"
	ls -la $(BUILD_DIR)/*.deb $(BUILD_DIR)/*.rpm 2>/dev/null || true

# System info
info: ## Show system information
	@echo "$(BLUE)System Information:$(NC)"
	@echo "OS: $$(cat /etc/os-release | grep PRETTY_NAME | cut -d= -f2 | tr -d '\"')"
	@echo "Kernel: $$(uname -r)"
	@echo "CPU: $$(lscpu | grep 'Model name' | cut -d: -f2 | xargs)"
	@echo "Cores: $(NUM_CORES)"
	@echo "Memory: $$(free -h | grep Mem | awk '{print $$2}')"
	@echo "Disk: $$(df -h . | tail -1 | awk '{print $$4}' | sed 's/Available/Free/')"
	@echo ""
	@echo "$(BLUE)Build Configuration:$(NC)"
	@echo "Build Type: $(BUILD_TYPE)"
	@echo "Build Directory: $(BUILD_DIR)"
	@echo ""
	@echo "$(BLUE)Dependency Status:$(NC)"
	@./build.sh check 2>/dev/null || echo "$(RED)Run 'make deps' to install dependencies$(NC)"

# Git hooks
install-hooks: ## Install Git pre-commit hooks
	@echo "$(BLUE)Installing Git hooks...$(NC)"
	echo '#!/bin/bash\nmake format\ngit add -u' > .git/hooks/pre-commit
	chmod +x .git/hooks/pre-commit
	@echo "$(GREEN)Git hooks installed$(NC)"

# Quick commands
q: run ## Quick run (alias for 'run')
qv: run-verbose ## Quick verbose run (alias for 'run-verbose')
qt: run-test ## Quick test run (alias for 'run-test')

# Status check
status: ## Show project status
	@echo "$(BLUE)Project Status:$(NC)"
	@if [ -d "$(BUILD_DIR)" ]; then \
		echo "$(GREEN)✓ Build directory exists$(NC)"; \
		if [ -f "$(BUILD_DIR)/bin/market_depth_processor" ]; then \
			echo "$(GREEN)✓ Binary built$(NC)"; \
			echo "Binary size: $$(du -h $(BUILD_DIR)/bin/market_depth_processor | cut -f1)"; \
		else \
			echo "$(RED)✗ Binary not built$(NC)"; \
		fi \
	else \
		echo "$(RED)✗ Build directory missing$(NC)"; \
	fi
	@echo ""
	@if docker-compose ps | grep -q "Up"; then \
		echo "$(GREEN)✓ Docker services running$(NC)"; \
		docker-compose ps; \
	else \
		echo "$(YELLOW)⚠ Docker services not running$(NC)"; \
	fi

# Maintenance
update-deps: ## Update all dependencies
	@echo "$(BLUE)Updating dependencies...$(NC)"
	sudo apt-get update && sudo apt-get upgrade -y || sudo yum update -y

backup-config: ## Backup configuration files
	@echo "$(BLUE)Backing up configuration...$(NC)"
	tar -czf config-backup-$$(date +%Y%m%d-%H%M%S).tar.gz config/

# For tab completion
_completion:
	@echo "help all deps check configure build test install clean docker run run-verbose run-test format lint docs"