#include <iostream>
#include <random>
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

#include "KafkaProducer.hpp"
#include "KafkaConsumer.hpp"

/**
 * @brief Get log filename by day
 * Helper function to get log filename for today.
 */
std::string get_log_filename(const std::string &log_folder) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << log_folder << "/log_";
    ss << std::put_time(std::localtime(&t), "%Y_%m_%d") << ".txt";
    return ss.str();
}

/**
 * @brief Setup log config
 * Function to setup spdlog.
 */
std::shared_ptr<spdlog::logger> setup_logger(
    spdlog::level::level_enum level = spdlog::level::info,
    const std::string &log_folder = "logs") {
    // Ensure log dir exists
    std::filesystem::create_directories(log_folder);

    // 50MB per file, 10,000 files (effectively unlimited)
    size_t max_file_size = 100 * 1024 * 1024; // 100MB
    size_t max_files = 10000;

    std::string filename = get_log_filename(log_folder);

    auto logger = spdlog::rotating_logger_mt("daily_size_logger", filename, max_file_size, max_files);

    // Set log pattern: [YYYY-MM-DD HH:MM:SS.us][LOG LEVEL][FILENAME][FUNCTION] log content
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f][%l][%s][%!] %v");

    // Set log level threshold
    logger->set_level(level);

    // Also set the level of ALL SINKS!
    for (auto &sink: logger->sinks())
        sink->set_level(level);

    spdlog::set_default_logger(logger);
    return logger;
}

/**
 * @brief Setup log level
 * Helper function to parse log level from string
 */
spdlog::level::level_enum parse_log_level(const std::string &level_str) {
    std::string lvl = level_str;
    std::transform(lvl.begin(), lvl.end(), lvl.begin(), ::tolower);

    if (lvl == "trace") return spdlog::level::trace;
    if (lvl == "debug") return spdlog::level::debug;
    if (lvl == "info") return spdlog::level::info;
    if (lvl == "warn") return spdlog::level::warn;
    if (lvl == "err" || lvl == "error") return spdlog::level::err;
    if (lvl == "critical") return spdlog::level::critical;
    if (lvl == "off") return spdlog::level::off;
    // Default
    return spdlog::level::info;
}

int main(int argc, char *argv[]) {
    std::string config_path = "config/config.yaml"; // Default config path
    std::string log_level_str = "INFO"; // Default log level
    std::string log_folder = "/tmp/"; // Default log folder

    // Naive POSIX-style flag parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    YAML::Node config_node;
    std::cout << "[MAIN] Loading global config" << std::endl;
    try {
        config_node = YAML::LoadFile(config_path);
    } catch (const YAML::Exception &ex) {
        SPDLOG_ERROR("Failed to load config file ({}); using default config.", ex.what());
        return 1;
    }

    if (config_node["global"]) {
        const auto &global = config_node["global"];
        if (global["log_level"]) log_level_str = global["log_level"].as<std::string>();
        if (global["log_path"]) log_folder = global["log_path"].as<std::string>();
    }
    spdlog::level::level_enum log_level = parse_log_level(log_level_str);
    auto logger = setup_logger(log_level, log_folder);
    spdlog::flush_every(std::chrono::seconds(1));

    // Load a config path from environment variable or use default
    std::string config_file_path;
    const char *config_env = std::getenv("CONFIG_PATH");
    if (config_env && std::strlen(config_env) > 0) {
        config_file_path = config_env;
    } else {
        config_file_path = "config/config.yaml"; // default
    }
    return 0;
}