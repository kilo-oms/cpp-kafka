cmake_minimum_required(VERSION 3.14)
project(MarketDepthProcessor CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler-specific optimization flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")
    # Enable additional warnings for better code quality
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
endif()

# Find required packages
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(PkgConfig REQUIRED)

# Find spdlog (with fallback to bundled version)
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    # Check if we have bundled spdlog in include/
    if(EXISTS "${CMAKE_SOURCE_DIR}/include/spdlog/spdlog.h")
        message(STATUS "Using bundled spdlog from include/spdlog")
        add_library(spdlog::spdlog INTERFACE IMPORTED
                src/OrderBookTypes.cpp
                include/FlatBuffersFormatter.hpp)
        target_include_directories(spdlog::spdlog INTERFACE ${CMAKE_SOURCE_DIR}/include)
        target_compile_definitions(spdlog::spdlog INTERFACE SPDLOG_HEADER_ONLY)
    else()
        # Try to find manually
        find_path(SPDLOG_INCLUDE_DIR spdlog/spdlog.h
                PATHS /usr/include /usr/local/include
        )
        if(SPDLOG_INCLUDE_DIR)
            message(STATUS "Found spdlog manually: ${SPDLOG_INCLUDE_DIR}")
            add_library(spdlog::spdlog INTERFACE IMPORTED
                    src/OrderBookTypes.cpp
                    include/FlatBuffersFormatter.hpp)
            target_include_directories(spdlog::spdlog INTERFACE ${SPDLOG_INCLUDE_DIR})
            target_compile_definitions(spdlog::spdlog INTERFACE SPDLOG_HEADER_ONLY)
        else()
            message(FATAL_ERROR "spdlog not found. Please install it or run ./fix_dependencies.sh")
        endif()
    endif()
endif()

# Find nlohmann_json (with fallback)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    # Try to find manually
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
            PATHS /usr/include /usr/local/include
    )
    if(NLOHMANN_JSON_INCLUDE_DIR)
        message(STATUS "Found nlohmann-json manually: ${NLOHMANN_JSON_INCLUDE_DIR}")
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED
                src/OrderBookTypes.cpp
                include/FlatBuffersFormatter.hpp)
        target_include_directories(nlohmann_json::nlohmann_json INTERFACE ${NLOHMANN_JSON_INCLUDE_DIR})
    else()
        message(FATAL_ERROR "nlohmann-json not found. Please install it or run ./fix_dependencies.sh")
    endif()
endif()

# Find FlatBuffers (with fallback)
find_package(Flatbuffers QUIET)
if(NOT Flatbuffers_FOUND)
    # Try to find manually
    find_path(FLATBUFFERS_INCLUDE_DIR flatbuffers/flatbuffers.h
            PATHS /usr/include /usr/local/include
    )
    find_library(FLATBUFFERS_LIBRARY flatbuffers
            PATHS /usr/lib /usr/local/lib
    )
    if(FLATBUFFERS_INCLUDE_DIR AND FLATBUFFERS_LIBRARY)
        message(STATUS "Found flatbuffers manually: ${FLATBUFFERS_INCLUDE_DIR}")
        add_library(flatbuffers::flatbuffers UNKNOWN IMPORTED
                src/OrderBookTypes.cpp
                include/FlatBuffersFormatter.hpp)
        set_target_properties(flatbuffers::flatbuffers PROPERTIES
                IMPORTED_LOCATION ${FLATBUFFERS_LIBRARY}
                INTERFACE_INCLUDE_DIRECTORIES ${FLATBUFFERS_INCLUDE_DIR}
        )
    else()
        message(FATAL_ERROR "flatbuffers not found. Please install it or run ./fix_dependencies.sh")
    endif()
endif()

# Find librdkafka using pkg-config
pkg_check_modules(RDKAFKA REQUIRED rdkafka rdkafka++)

# Include directories
include_directories(
        ${CMAKE_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
        ${RDKAFKA_INCLUDE_DIRS}
)

# Source files for the main executable
set(MAIN_SRC_FILES
        src/main.cpp
        src/KafkaConsumer.cpp
        src/KafkaProducer.cpp
        src/OrderBookTypes.cpp
        src/OrderBook.cpp
        src/MessageFactory.cpp
        src/MarketDepthProcessor.cpp
        src/OrderBookTypes.cpp
        include/FlatBuffersFormatter.hpp
)

# Header files (for IDE support)
set(HEADER_FILES
        include/KafkaConsumer.hpp
        include/KafkaProducer.hpp
        include/KafkaPush.hpp
        include/OrderBookTypes.hpp
        include/OrderBook.hpp
        include/MessageFactory.hpp
        include/MarketDepthProcessor.hpp
        include/orderbook_generated.h
        src/OrderBookTypes.cpp
        include/FlatBuffersFormatter.hpp
)

# Create main executable
add_executable(market_depth_processor ${MAIN_SRC_FILES} ${HEADER_FILES}
        src/OrderBookTypes.cpp
        include/FlatBuffersFormatter.hpp)

# Set target properties
set_target_properties(market_depth_processor PROPERTIES
        OUTPUT_NAME "market_depth_processor"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Include directories for the target
target_include_directories(market_depth_processor
        PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
        ${RDKAFKA_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(market_depth_processor
        PRIVATE
        Threads::Threads
        yaml-cpp
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        flatbuffers::flatbuffers
        ${RDKAFKA_LIBRARIES}
)

# Add linker flags for rdkafka
target_link_directories(market_depth_processor
        PRIVATE
        ${RDKAFKA_LIBRARY_DIRS}
)

# Compiler definitions
target_compile_definitions(market_depth_processor
        PRIVATE
        ${RDKAFKA_CFLAGS_OTHER}
)

# Custom target for generating FlatBuffers headers (optional - if you want to regenerate)
find_program(FLATC flatc)
if(FLATC)
    add_custom_target(generate_flatbuffers
            COMMAND ${FLATC} --cpp --gen-object-api -o ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/flatbuffers/orderbook.fbs
            COMMENT "Generating FlatBuffers headers"
            SOURCES ${CMAKE_SOURCE_DIR}/flatbuffers/orderbook.fbs
    )
    add_dependencies(market_depth_processor generate_flatbuffers)
endif()

# Install targets
install(TARGETS market_depth_processor
        RUNTIME DESTINATION bin
)

# Install configuration files
install(DIRECTORY config/
        DESTINATION etc/market_depth_processor
        FILES_MATCHING PATTERN "*.yaml"
)

# Install FlatBuffers schema
install(FILES flatbuffers/orderbook.fbs
        DESTINATION share/market_depth_processor/schema
)

# Create package configuration
set(CPACK_PACKAGE_NAME "market-depth-processor")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-frequency CBOE market depth processor")
set(CPACK_PACKAGE_VENDOR "Equix Technologies Pty Ltd")
set(CPACK_PACKAGE_CONTACT "contact@equix.com.au")

# DEB package configuration
set(CPACK_DEBIAN_PACKAGE_DEPENDS "librdkafka1, libyaml-cpp0.6, libspdlog1, libflatbuffers1")
set(CPACK_DEBIAN_PACKAGE_SECTION "finance")

# RPM package configuration
set(CPACK_RPM_PACKAGE_GROUP "Applications/Finance")
set(CPACK_RPM_PACKAGE_REQUIRES "librdkafka, yaml-cpp, spdlog, flatbuffers")

include(CPack)

# Development targets
add_custom_target(run
        COMMAND ${CMAKE_BINARY_DIR}/bin/market_depth_processor -c ${CMAKE_SOURCE_DIR}/config/config.yaml
        DEPENDS market_depth_processor
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running market depth processor with default config"
)

add_custom_target(run_verbose
        COMMAND ${CMAKE_BINARY_DIR}/bin/market_depth_processor -c ${CMAKE_SOURCE_DIR}/config/config.yaml -v
        DEPENDS market_depth_processor
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running market depth processor in verbose mode"
)

add_custom_target(run_test
        COMMAND ${CMAKE_BINARY_DIR}/bin/market_depth_processor -c ${CMAKE_SOURCE_DIR}/config/config.yaml --runtime 60 -v
        DEPENDS market_depth_processor
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running market depth processor for 60 seconds in test mode"
)

# Documentation target (if Doxygen is available)
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_BINARY_DIR}/Doxyfile)

    if(EXISTS ${DOXYGEN_IN})
        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

        add_custom_target(docs
                COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Generating API documentation with Doxygen"
                VERBATIM
        )
    endif()
endif()

# Print build information
message(STATUS "=== Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Dependencies found:")
message(STATUS "  - Boost: ${Boost_VERSION}")
message(STATUS "  - yaml-cpp: Found")
message(STATUS "  - spdlog: Found")
message(STATUS "  - nlohmann_json: Found")
message(STATUS "  - flatbuffers: Found")
message(STATUS "  - rdkafka: ${RDKAFKA_VERSION}")
if(FLATC)
    message(STATUS "  - flatc: ${FLATC}")
else()
    message(STATUS "  - flatc: Not found (FlatBuffer headers won't be regenerated)")
endif()
message(STATUS "===============================")

# Validation target to check if all dependencies are properly linked
add_custom_target(validate
        COMMAND ${CMAKE_BINARY_DIR}/bin/market_depth_processor --help
        DEPENDS market_depth_processor
        COMMENT "Validating executable can run and show help"
)