CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -pthread -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE

# Detect OS (Darwin = macOS)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    YAML_INCLUDE = -I/opt/homebrew/include
    YAML_LIB     = -L/opt/homebrew/lib
    KAFKA_INCLUDE = -I/opt/homebrew/include
    KAFKA_LIB     = -L/opt/homebrew/lib
    FLATBUF_INCLUDE = -I/opt/homebrew/include
    FLATBUF_LIB     = -L/opt/homebrew/lib
    JSON_INCLUDE = -I/opt/homebrew/include
    JSON_LIB     = -L/opt/homebrew/lib
else
    YAML_INCLUDE = -I/usr/local/include
    YAML_LIB     = -L/usr/local/lib
    KAFKA_INCLUDE = -I/usr/local/include
    KAFKA_LIB     = -L/usr/local/lib
    FLATBUF_INCLUDE = -I/usr/local/include
    FLATBUF_LIB     = -L/usr/local/lib
    JSON_INCLUDE = -I/usr/local/include
    JSON_LIB     = -L/usr/local/lib
endif

INCLUDES = -I./include $(YAML_INCLUDE) $(KAFKA_INCLUDE) $(FLATBUF_INCLUDE) $(JSON_INCLUDE)
LIBS     = $(YAML_LIB) $(KAFKA_LIB) $(FLATBUF_LIB) $(JSON_LIB) -lyaml-cpp -lrdkafka -lflatbuffers -lpthread

TARGET = simplified_market_depth_processor

SRCDIR = ./src
OBJDIR = ./obj
BINDIR = ./bin
CONFIGDIR = ./config
FLATBUFDIR = ./flatbuffers

# Simplified source files - removed OrderBook dependencies
SOURCES = main.cpp \
          KafkaConsumer.cpp \
          KafkaProducer.cpp \
          MarketDepthProcessor.cpp \
          MessageFactory.cpp \
          OrderBookTypes.cpp

OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(SOURCES))

# FlatBuffers schema file
FLATBUF_SCHEMA = $(FLATBUFDIR)/orderbook.fbs
FLATBUF_GENERATED = ./include/orderbook_generated.h

all: $(BINDIR)/$(TARGET)

# Generate FlatBuffers headers if schema exists
$(FLATBUF_GENERATED): $(FLATBUF_SCHEMA)
	flatc --cpp -o ./include $(FLATBUF_SCHEMA)

# Main target
$(BINDIR)/$(TARGET): $(OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# Object file compilation
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR) $(FLATBUF_GENERATED)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# Directory creation
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

# Dependencies for header files - simplified
$(OBJDIR)/main.o: $(SRCDIR)/main.cpp \
                  ./include/MarketDepthProcessor.hpp \
                  ./include/KafkaConsumer.hpp \
                  ./include/KafkaProducer.hpp

$(OBJDIR)/MarketDepthProcessor.o: $(SRCDIR)/MarketDepthProcessor.cpp \
                                  ./include/MarketDepthProcessor.hpp \
                                  ./include/MessageFactory.hpp \
                                  ./include/KafkaConsumer.hpp \
                                  ./include/KafkaProducer.hpp \
                                  ./include/KafkaPush.hpp \
                                  ./include/orderbook_generated.h

$(OBJDIR)/KafkaConsumer.o: $(SRCDIR)/KafkaConsumer.cpp \
                           ./include/KafkaConsumer.hpp

$(OBJDIR)/KafkaProducer.o: $(SRCDIR)/KafkaProducer.cpp \
                           ./include/KafkaProducer.hpp

$(OBJDIR)/MessageFactory.o: $(SRCDIR)/MessageFactory.cpp \
                            ./include/MessageFactory.hpp \
                            ./include/OrderBookTypes.hpp

$(OBJDIR)/OrderBookTypes.o: $(SRCDIR)/OrderBookTypes.cpp \
                            ./include/OrderBookTypes.hpp

# Generate Python bindings from FlatBuffers (optional)
python-gen: $(FLATBUF_SCHEMA)
	flatc --python -o ./python_generated $(FLATBUF_SCHEMA)

# Utility targets
install: $(BINDIR)/$(TARGET)
	cp $(BINDIR)/$(TARGET) /usr/local/bin/

run: $(BINDIR)/$(TARGET)
	$(BINDIR)/$(TARGET) -c $(CONFIGDIR)/config.yaml

run-verbose: $(BINDIR)/$(TARGET)
	$(BINDIR)/$(TARGET) -c $(CONFIGDIR)/config.yaml -v

run-test: $(BINDIR)/$(TARGET)
	$(BINDIR)/$(TARGET) -c $(CONFIGDIR)/config.yaml --runtime 60 -v

run-debug: $(BINDIR)/$(TARGET)
	gdb --args $(BINDIR)/$(TARGET) $(CONFIGDIR)/config.yaml

# Build modes
debug: CXXFLAGS += -DDEBUG -g -O0
debug: clean $(BINDIR)/$(TARGET)

release: CXXFLAGS += -DNDEBUG -O3 -march=native -mtune=native
release: clean $(BINDIR)/$(TARGET)

# Development utilities
check-deps: check_deps.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o check_deps check_deps.cpp $(LIBS)
	./check_deps

format:
	find ./src ./include -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i

lint:
	find ./src ./include -name "*.cpp" -o -name "*.hpp" | xargs cppcheck --enable=all

# Docker targets
docker-build:
	docker build -t simplified-market-depth-processor .

docker-run: docker-build
	docker run -v $(PWD)/config:/app/config simplified-market-depth-processor

# Clean targets
clean:
	rm -f $(OBJDIR)/*.o $(BINDIR)/$(TARGET)
	rm -f check_deps

clean-generated:
	rm -f $(FLATBUF_GENERATED)
	rm -rf ./python_generated/md

distclean: clean clean-generated
	rm -rf $(OBJDIR) $(BINDIR)

# Generate all artifacts
generate: $(FLATBUF_GENERATED) python-gen

# Full rebuild
rebuild: distclean generate all

# Test with sample data (requires test data generator)
test-with-data: $(BINDIR)/$(TARGET)
	@echo "Starting simplified processor with test data..."
	@echo "This requires ORDERBOOK topic to have test data"
	$(BINDIR)/$(TARGET) -c $(CONFIGDIR)/config.yaml --runtime 300 -v

# Performance test
perf-test: $(BINDIR)/$(TARGET)
	@echo "Running performance test for 60 seconds..."
	time $(BINDIR)/$(TARGET) -c $(CONFIGDIR)/config.yaml --runtime 60 --stats-interval 10

help:
	@echo "Available targets:"
	@echo "  all              - Build simplified market depth processor (default)"
	@echo "  debug            - Build with debug symbols"
	@echo "  release          - Build optimized release version"
	@echo "  install          - Install to /usr/local/bin"
	@echo "  run              - Build and run with config/config.yaml"
	@echo "  run-verbose      - Build and run with verbose logging"
	@echo "  run-test         - Build and run for 60 seconds in test mode"
	@echo "  run-debug        - Run with gdb debugger"
	@echo "  test-with-data   - Run with sample data for 5 minutes"
	@echo "  perf-test        - Run performance test for 60 seconds"
	@echo "  check-deps       - Check system dependencies"
	@echo "  format           - Format code with clang-format"
	@echo "  lint             - Run cppcheck static analysis"
	@echo "  generate         - Generate FlatBuffers headers and Python bindings"
	@echo "  python-gen       - Generate Python FlatBuffers bindings"
	@echo "  docker-build     - Build Docker image"
	@echo "  docker-run       - Build and run in Docker"
	@echo "  clean            - Remove object files and binaries"
	@echo "  clean-generated  - Remove generated files"
	@echo "  distclean        - Remove all generated files and directories"
	@echo "  rebuild          - Full clean rebuild with code generation"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Simplified Market Depth Processor Features:"
	@echo "  - Direct FlatBuffers snapshot processing"
	@echo "  - No order book state management"
	@echo "  - No CDC event generation"
	@echo "  - Output to market_depth.[SYMBOL_NAME] topics"
	@echo "  - 8-partition consumption with symbol-based routing"

.PHONY: all debug release install run run-verbose run-test run-debug test-with-data perf-test check-deps format lint generate python-gen docker-build docker-run clean clean-generated distclean rebuild help