/**
 * @file check_deps.cpp
 * @brief Simple dependency verification test
 *
 * This program tests if all required dependencies are properly installed
 * and can be compiled/linked correctly.
 */

#include <iostream>
#include <string>
#include <vector>

// Test spdlog
#ifdef SPDLOG_VERSION
    #include "spdlog/spdlog.h"
#else
    #include <spdlog/spdlog.h>
#endif

// Test nlohmann-json
#include <nlohmann/json.hpp>

// Test flatbuffers
#include <flatbuffers/flatbuffers.h>

// Test Boost
#include <boost/version.hpp>

// Test yaml-cpp
#include <yaml-cpp/yaml.h>

// Test rdkafka
#include <librdkafka/rdkafka.h>

int main() {
    std::cout << "=== Dependency Verification Test ===" << std::endl;
    std::cout << std::endl;

    bool all_good = true;

    // Test spdlog
    try {
        spdlog::info("Testing spdlog functionality");
        std::cout << "✓ spdlog: Working" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ spdlog: Error - " << e.what() << std::endl;
        all_good = false;
    }

    // Test nlohmann-json
    try {
        nlohmann::json test_json;
        test_json["test"] = "value";
        test_json["number"] = 42;
        std::cout << "✓ nlohmann-json: Working (version " << NLOHMANN_JSON_VERSION_MAJOR
                  << "." << NLOHMANN_JSON_VERSION_MINOR
                  << "." << NLOHMANN_JSON_VERSION_PATCH << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ nlohmann-json: Error - " << e.what() << std::endl;
        all_good = false;
    }

    // Test flatbuffers
    try {
        flatbuffers::FlatBufferBuilder builder(1024);
        std::cout << "✓ flatbuffers: Working (version " << FLATBUFFERS_VERSION_MAJOR
                  << "." << FLATBUFFERS_VERSION_MINOR
                  << "." << FLATBUFFERS_VERSION_REVISION << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ flatbuffers: Error - " << e.what() << std::endl;
        all_good = false;
    }

    // Test Boost
    try {
        std::cout << "✓ Boost: Working (version " << BOOST_VERSION / 100000
                  << "." << BOOST_VERSION / 100 % 1000
                  << "." << BOOST_VERSION % 100 << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ Boost: Error - " << e.what() << std::endl;
        all_good = false;
    }

    // Test yaml-cpp
    try {
        YAML::Node test_yaml;
        test_yaml["test"] = "value";
        std::cout << "✓ yaml-cpp: Working" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ yaml-cpp: Error - " << e.what() << std::endl;
        all_good = false;
    }

    // Test rdkafka
    try {
        const char* version = rd_kafka_version_str();
        std::cout << "✓ librdkafka: Working (version " << version << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ librdkafka: Error - " << e.what() << std::endl;
        all_good = false;
    }

    std::cout << std::endl;

    if (all_good) {
        std::cout << "🎉 All dependencies are working correctly!" << std::endl;
        std::cout << "You can now build the market depth processor." << std::endl;
        return 0;
    } else {
        std::cout << "❌ Some dependencies have issues." << std::endl;
        std::cout << "Please run ./fix_dependencies.sh to resolve them." << std::endl;
        return 1;
    }
}