/**
 * @file    MarketDepthProcessor.hpp
 * @brief   Main market depth processing engine
 *
 * Developer: Equix Technologies
 * Copyright: Equix Technologies Pty Ltd
 * Created: June 2025
 *
 * Description:
 *   Main processing engine that orchestrates message consumption from Kafka,
 *   order book state management, CDC generation, and publishing of snapshots
 *   and CDC events. Designed for high-throughput, low-latency processing.
 */

#pragma once

#ifndef MARKET_DEPTH_PROCESSOR_HPP_
#define MARKET_DEPTH_PROCESSOR_HPP_

#include "OrderBook.hpp"
#include "MessageFactory.hpp"
#include "KafkaConsumer.hpp"
#include "KafkaProducer.hpp"
#include "KafkaPush.hpp"
#include "orderbook_generated.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

namespace market_depth {

// Forward declare FlatBuffers types
namespace fb = ::md;

/**
 * @brief Configuration for the market depth processor
 */
struct ProcessorConfig {
    // Kafka configuration
    std::string kafka_config_path;
    std::string input_topic;
    int consumer_poll_timeout_ms;
    int max_processing_threads;

    // Order book configuration
    DepthConfig depth_config;

    // Message factory configuration
    MessageFactory::JsonConfig json_config;

    // Message routing configuration
    MessageRouter::TopicConfig topic_config;

    // Processing configuration
    uint32_t max_messages_per_batch;
    uint32_t flush_interval_ms;
    bool enable_statistics;
    uint32_t stats_report_interval_s;

    // Performance tuning
    bool use_symbol_threading;
    uint32_t message_queue_size;
    bool enable_back_pressure;

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
 * @brief Main market depth processor
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
     * @brief Main processing loop
     */
    void processing_loop();

    /**
     * @brief Process a single Kafka message
     */
    bool process_message(rd_kafka_message_t* msg);

    /**
     * @brief Handle CDC event callback
     */
    void on_cdc_event(const CDCEvent& event);

    /**
     * @brief Publish snapshot messages for all depth levels
     */
    void publish_snapshots(const InternalOrderBookSnapshot &snapshot);

    /**
     * @brief Publish CDC event message
     */
    void publish_cdc_event(const CDCEvent& event);

    /**
     * @brief Statistics reporting thread
     */
    void stats_thread();

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
    std::unique_ptr<OrderBookManager> orderbook_manager_;
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

} // namespace md

#endif /* MARKET_DEPTH_PROCESSOR_HPP_ */