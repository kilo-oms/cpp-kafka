/**
 * @file    main.cpp
 * @brief   Simplified Market Depth Processor - Main Entry Point
 *
 * Description:
 *   Simplified version that processes FlatBuffers snapshots directly
 *   and publishes to market_depth.[SYMBOL_NAME] topics without maintaining
 *   order book state or CDC functionality.
 */

#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <atomic>
#include <yaml-cpp/yaml.h>

/* SpdLog library. */
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"

/* Simplified Market Depth Processing Components */
#include "MarketDepthProcessor.hpp"
#include "KafkaProducer.hpp"
#include "KafkaConsumer.hpp"

/**
 * @brief Print application banner and version info
 */
void print_banner() {
    std::cout << R"(
╔══════════════════════════════════════════════════════════════╗
║          Simplified CBOE Market Depth Processor v1.0         ║
║                   Equix Technologies Pty Ltd                 ║
╠══════════════════════════════════════════════════════════════╣
║  Simplified high-frequency market data processing            ║
║  Input: CBOE L2 snapshots (FlatBuffers via Kafka)            ║
║  Output: Multi-depth JSON to market_depth.[SYMBOL] topics    ║
║  Features: Direct processing, 8-partition consumption        ║
║  Disabled: Order book state, CDC events                      ║
╚══════════════════════════════════════════════════════════════╝
)" << std::endl;
}

/**
 * @brief Print usage information
 */
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  -c, --config PATH     Configuration file path (default: config/config.yaml)\n"
              << "  -t, --topic TOPIC     Input Kafka topic (default: from config)\n"
              << "  -r, --runtime SECONDS Maximum runtime in seconds (0 = infinite)\n"
              << "  -d, --depths LEVELS   Comma-separated depth levels (e.g., 5,10,25,50)\n"
              << "  --stats-interval SEC  Statistics reporting interval (default: 30)\n"
              << "  -v, --verbose        Enable verbose logging (debug level)\n"
              << "  -q, --quiet          Quiet mode (warnings and errors only)\n"
              << "  -h, --help           Show this help message\n\n"
              << "Examples:\n"
              << "  " << program_name << " -c config/prod.yaml -t ORDERBOOK\n"
              << "  " << program_name << " --runtime 3600 --depths 5,10,25\n"
              << "  " << program_name << " -v --stats-interval 10\n\n"
              << "Note: CDC and order book state management are disabled in this version.\n\n";
}

/**
 * @brief Get log filename by day
 */
std::string get_log_filename(const std::string &log_folder) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << log_folder << "/market_depth_";
    ss << std::put_time(std::localtime(&t), "%Y_%m_%d") << ".log";
    return ss.str();
}

/**
 * @brief Setup logging with rotation
 */
std::shared_ptr<spdlog::logger> setup_logger(
    spdlog::level::level_enum level = spdlog::level::info,
    const std::string &log_folder = "logs") {

    // Ensure log directory exists
    std::filesystem::create_directories(log_folder);

    // 100MB per file, 50 files max (5GB total)
    size_t max_file_size = 100 * 1024 * 1024;
    size_t max_files = 50;

    std::string filename = get_log_filename(log_folder);
    auto logger = spdlog::rotating_logger_mt("market_depth_logger", filename, max_file_size, max_files);

    // Enhanced log pattern with thread ID and microsecond precision
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f][%t][%l][%s:%#][%!] %v");
    logger->set_level(level);

    // Set level for all sinks
    for (auto &sink: logger->sinks()) {
        sink->set_level(level);
    }

    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(3));
    return logger;
}

/**
 * @brief Parse log level from string
 */
spdlog::level::level_enum parse_log_level(const std::string &level_str) {
    std::string lvl = level_str;
    std::transform(lvl.begin(), lvl.end(), lvl.begin(), ::tolower);

    if (lvl == "trace") return spdlog::level::trace;
    if (lvl == "debug") return spdlog::level::debug;
    if (lvl == "info") return spdlog::level::info;
    if (lvl == "warn" || lvl == "warning") return spdlog::level::warn;
    if (lvl == "err" || lvl == "error") return spdlog::level::err;
    if (lvl == "critical") return spdlog::level::critical;
    if (lvl == "off") return spdlog::level::off;

    return spdlog::level::info; // Default
}

/**
 * @brief Parse comma-separated depth levels
 */
std::vector<uint32_t> parse_depth_levels(const std::string& depth_str) {
    std::vector<uint32_t> levels;
    std::stringstream ss(depth_str);
    std::string item;

    while (std::getline(ss, item, ',')) {
        try {
            uint32_t level = static_cast<uint32_t>(std::stoul(item));
            if (level > 0 && level <= 1000) {
                levels.push_back(level);
            } else {
                SPDLOG_WARN("Invalid depth level ignored: {}", level);
            }
        } catch (const std::exception& e) {
            SPDLOG_WARN("Failed to parse depth level '{}': {}", item, e.what());
        }
    }

    if (levels.empty()) {
        SPDLOG_WARN("No valid depth levels parsed, using defaults");
        levels = {5, 10, 25, 50};
    }

    return levels;
}

/**
 * @brief Load simplified processor configuration from YAML and command line
 */
market_depth::ProcessorConfig load_processor_config(const std::string& config_path,
                                         const std::map<std::string, std::string>& cli_overrides) {
    market_depth::ProcessorConfig config;

    try {
        YAML::Node yaml_config = YAML::LoadFile(config_path);

        // Load Kafka configuration
        config.kafka_config_path = config_path;

        // Load from YAML with defaults
        if (yaml_config["processor"]) {
            const auto& proc = yaml_config["processor"];
            config.input_topic = proc["input_topic"] ? proc["input_topic"].as<std::string>() : "ORDERBOOK";
            config.consumer_poll_timeout_ms = proc["poll_timeout_ms"] ? proc["poll_timeout_ms"].as<int>() : 100;
            config.num_partitions = proc["num_partitions"] ? proc["num_partitions"].as<int>() : 8;
            config.flush_interval_ms = proc["flush_interval_ms"] ? proc["flush_interval_ms"].as<uint32_t>() : 1000;
            config.stats_report_interval_s = proc["stats_interval_s"] ? proc["stats_interval_s"].as<uint32_t>() : 30;
        }

        // Load depth configuration (simplified - no CDC)
        if (yaml_config["depth_config"]) {
            const auto& depth = yaml_config["depth_config"];
            if (depth["levels"]) {
                config.depth_levels = depth["levels"].as<std::vector<uint32_t>>();
            }
        }

        // Load JSON formatting configuration
        if (yaml_config["json_config"]) {
            const auto& json = yaml_config["json_config"];
            config.json_config.price_decimals = json["price_decimals"] ? json["price_decimals"].as<uint32_t>() : 4;
            config.json_config.quantity_decimals = json["quantity_decimals"] ? json["quantity_decimals"].as<uint32_t>() : 2;
            config.json_config.include_timestamp = json["include_timestamp"] ? json["include_timestamp"].as<bool>() : true;
            config.json_config.include_sequence = json["include_sequence"] ? json["include_sequence"].as<bool>() : true;
            config.json_config.compact_format = json["compact_format"] ? json["compact_format"].as<bool>() : false;
            config.json_config.exchange_name = json["exchange_name"] ? json["exchange_name"].as<std::string>() : "CXA";
        }

        // Load simplified topic routing configuration
        if (yaml_config["topic_config"]) {
            const auto& topic = yaml_config["topic_config"];
            config.topic_config.snapshot_topic_prefix = topic["snapshot_prefix"] ? topic["snapshot_prefix"].as<std::string>() : "market_depth.";
            config.topic_config.use_symbol_partitioning = topic["use_symbol_partitioning"] ? topic["use_symbol_partitioning"].as<bool>() : true;
            config.topic_config.num_partitions = topic["num_partitions"] ? topic["num_partitions"].as<uint32_t>() : 8;
        }

    } catch (const YAML::Exception& e) {
        SPDLOG_WARN("Failed to load YAML config: {}. Using defaults.", e.what());
    }

    // Apply command line overrides
    for (const auto& [key, value] : cli_overrides) {
        if (key == "topic") {
            config.input_topic = value;
        } else if (key == "depths") {
            config.depth_levels = parse_depth_levels(value);
        } else if (key == "stats_interval") {
            config.stats_report_interval_s = static_cast<uint32_t>(std::stoul(value));
        }
    }

    return config;
}

/**
 * @brief Main entry point
 */
int main(int argc, char *argv[]) {
    print_banner();

    // Parse command line arguments
    std::string config_path = "config/config.yaml";
    std::string log_level_str = "info";
    std::string log_folder = "/tmp";
    uint32_t max_runtime_s = 0;
    std::map<std::string, std::string> cli_overrides;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if ((arg == "-h" || arg == "--help")) {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if ((arg == "-t" || arg == "--topic") && i + 1 < argc) {
            cli_overrides["topic"] = argv[++i];
        } else if ((arg == "-r" || arg == "--runtime") && i + 1 < argc) {
            max_runtime_s = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if ((arg == "-d" || arg == "--depths") && i + 1 < argc) {
            cli_overrides["depths"] = argv[++i];
        } else if (arg == "--stats-interval" && i + 1 < argc) {
            cli_overrides["stats_interval"] = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            log_level_str = "debug";
        } else if (arg == "-q" || arg == "--quiet") {
            log_level_str = "warn";
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Load global configuration for logging
    try {
        YAML::Node global_config = YAML::LoadFile(config_path);
        if (global_config["global"]) {
            const auto& global = global_config["global"];
            if (global["log_level"]) log_level_str = global["log_level"].as<std::string>();
            if (global["log_path"]) log_folder = global["log_path"].as<std::string>();
        }
    } catch (const YAML::Exception& e) {
        std::cerr << "Warning: Failed to load config for logging: " << e.what() << std::endl;
    }

    // Setup logging
    spdlog::level::level_enum log_level = parse_log_level(log_level_str);
    auto logger = setup_logger(log_level, log_folder);

    SPDLOG_INFO("Simplified Market Depth Processor starting...");
    SPDLOG_INFO("Config: {}, Log level: {}, Max runtime: {}s", config_path, log_level_str, max_runtime_s);

    try {
        // Load processor configuration
        auto config = load_processor_config(config_path, cli_overrides);

        SPDLOG_INFO("Simplified processor config loaded: input_topic={}, partitions={}, depth_levels=[{}]",
                   config.input_topic,
                   config.num_partitions,
                   [&]() {
                       std::string levels;
                       for (size_t i = 0; i < config.depth_levels.size(); ++i) {
                           if (i > 0) levels += ",";
                           levels += std::to_string(config.depth_levels[i]);
                       }
                       return levels;
                   }());

        SPDLOG_INFO("Output format: market_depth.[SYMBOL_NAME] topics with symbol-based partitioning");
        SPDLOG_INFO("Features: Direct snapshot processing, No order book state, No CDC events");

        // Create and initialize simplified processor
        market_depth::MarketDepthProcessor processor(config);

        if (!processor.initialize()) {
            SPDLOG_ERROR("Failed to initialize simplified processor");
            return 1;
        }

        // Setup graceful shutdown handler
        market_depth::ProcessorShutdownHandler shutdown_handler(processor);

        // Start processing (blocking call)
        processor.start_processing(max_runtime_s);

        SPDLOG_INFO("Simplified Market Depth Processor finished successfully");
        return 0;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Fatal error: {}", e.what());
        return 1;
    } catch (...) {
        SPDLOG_ERROR("Unknown fatal error occurred");
        return 1;
    }
}