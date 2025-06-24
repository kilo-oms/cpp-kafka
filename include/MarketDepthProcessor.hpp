/**
 * @file    MarketDepthProcessor.hpp
 * @brief   Simplified market depth processing engine - direct snapshot processing
 *
 * Developer: Equix Technologies
 * Copyright: Equix Technologies Pty Ltd
 * Created: June 2025
 *
 * Description:
 *   Simplified processing engine that consumes FlatBuffers snapshots directly
 *   and publishes multi-depth JSON messages without maintaining order book state.
 *   Designed for real-time processing with 8-partition consumption.
 */

#pragma once

#ifndef MARKET_DEPTH_PROCESSOR_HPP_
#define MARKET_DEPTH_PROCESSOR_HPP_

#include "MessageFactory.hpp"
#include "KafkaConsumer.hpp"
#include "KafkaProducer.hpp"
#include "KafkaPush.hpp"
#include "orderbook_generated.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

namespace market_depth {

// Forward declare FlatBuffers types
namespace fb = ::md;

/**
 * @brief Simplified configuration for the market depth processor
 */
struct ProcessorConfig {
    // Kafka configuration
    std::string kafka_config_path;
    std::string input_topic;
    int consumer_poll_timeout_ms;
    int num_partitions;  // Number of partitions to consume (8)

    // Depth configuration
    std::vector<uint32_t> depth_levels;

    // Message factory configuration
    MessageFactory::JsonConfig json_config;

    // Message routing configuration
    MessageRouter::TopicConfig topic_config;

    // Processing configuration
    uint32_t flush_interval_ms;
    bool enable_statistics;
    uint32_t stats_report_interval_s;

    ProcessorConfig();
};

/**
 * @brief Performance metrics for monitoring
 */
struct PerformanceMetrics {
    std::atomic<uint64_t> messages_consumed{0};
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> messages_published{0};
    std::atomic<uint64_t> processing_errors{0};
    std::atomic<uint64_t> kafka_errors{0};

    std::atomic<uint64_t> total_processing_time_us{0};
    std::atomic<uint64_t> max_processing_time_us{0};
    std::atomic<uint64_t> min_processing_time_us{UINT64_MAX};

    // Per-symbol metrics
    std::unordered_map<std::string, std::atomic<uint64_t>> symbol_message_counts;

    // Timing
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point last_stats_time;

    void reset() {
        messages_consumed = 0;
        messages_processed = 0;
        messages_published = 0;
        processing_errors = 0;
        kafka_errors = 0;
        total_processing_time_us = 0;
        max_processing_time_us = 0;
        min_processing_time_us = UINT64_MAX;
        symbol_message_counts.clear();
        start_time = std::chrono::high_resolution_clock::now();
        last_stats_time = start_time;
    }

    void update_processing_time(uint64_t time_us) {
        total_processing_time_us += time_us;

        uint64_t current_max = max_processing_time_us.load();
        while (time_us > current_max && !max_processing_time_us.compare_exchange_weak(current_max, time_us));

        uint64_t current_min = min_processing_time_us.load();
        while (time_us < current_min && !min_processing_time_us.compare_exchange_weak(current_min, time_us));
    }
};

/**
 * @brief Simplified market depth processor
 */
class MarketDepthProcessor {
public:
    explicit MarketDepthProcessor(const ProcessorConfig& config = ProcessorConfig{});
    ~MarketDepthProcessor();

    /**
     * @brief Initialize the processor (Kafka connections, etc.)
     */
    bool initialize();

    /**
     * @brief Start processing (blocking call)
     * @param max_runtime_s Maximum runtime in seconds (0 = infinite)
     */
    void start_processing(uint32_t max_runtime_s = 0);

    /**
     * @brief Stop processing gracefully
     */
    void stop_processing();

    /**
     * @brief Get current performance metrics
     */
    PerformanceMetrics get_metrics() const;

    /**
     * @brief Print performance statistics
     */
    void print_statistics() const;

    /**
     * @brief Check if processor is running
     */
    bool is_running() const { return running_; }

private:
    /**
     * @brief Main processing loop for a specific partition
     */
    void processing_loop();

    /**
     * @brief Process a single Kafka message
     */
    bool process_message(rd_kafka_message_t* msg);

    /**
     * @brief Process FlatBuffers snapshot and publish directly
     */
    bool process_snapshot(const fb::OrderBookSnapshot* snapshot);

    /**
     * @brief Publish snapshot messages for all depth levels
     */
    void publish_snapshots(const std::string& symbol, const fb::OrderBookSnapshot* snapshot);

    /**
     * @brief Statistics reporting thread
     */
    void stats_thread();

    /**
     * @brief Convert FlatBuffers price level to internal format
     */
    PriceLevel convert_price_level(const fb::OrderMsgLevel* fb_level) const;

    /**
     * @brief Get current timestamp in microseconds
     */
    static uint64_t get_timestamp() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }

private:
    ProcessorConfig config_;

    // Core components
    std::unique_ptr<MessageFactory> message_factory_;
    std::unique_ptr<MessageRouter> message_router_;

    // Threading and control
    std::atomic<bool> running_;
    std::atomic<bool> should_stop_;
    std::thread processing_thread_;
    std::thread stats_thread_;

    // Performance metrics
    mutable std::mutex metrics_mutex_;
    PerformanceMetrics metrics_;

    // Message batching
    std::chrono::high_resolution_clock::time_point last_flush_time_;
};

/**
 * @brief RAII wrapper for graceful shutdown handling
 */
class ProcessorShutdownHandler {
public:
    explicit ProcessorShutdownHandler(MarketDepthProcessor& processor);
    ~ProcessorShutdownHandler();

private:
    MarketDepthProcessor& processor_;
    static void signal_handler(int signal);
    static ProcessorShutdownHandler* instance_;
};

} // namespace market_depth

#endif /* MARKET_DEPTH_PROCESSOR_HPP_ */