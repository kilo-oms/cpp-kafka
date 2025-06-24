/**
 * @file    MarketDepthProcessor.cpp
 * @brief   Simplified market depth processor implementation
 */

#include "MarketDepthProcessor.hpp"
#include "spdlog/spdlog.h"
#include <signal.h>
#include <flatbuffers/flatbuffers.h>

namespace market_depth {

    // ProcessorConfig implementation
    ProcessorConfig::ProcessorConfig()
        : kafka_config_path("config/config.yaml")
          , input_topic("market_depth_input")
          , consumer_poll_timeout_ms(100)
          , num_partitions(8)
          , depth_levels({5, 10, 25, 50})
          , flush_interval_ms(1000)
          , enable_statistics(true)
          , stats_report_interval_s(30) {
    }

    MarketDepthProcessor::MarketDepthProcessor(const ProcessorConfig &config)
        : config_(config)
          , running_(false)
          , should_stop_(false)
          , last_flush_time_(std::chrono::high_resolution_clock::now()) {
        SPDLOG_INFO("MarketDepthProcessor created with config: input_topic={}, partitions={}, depth_levels=[{}]",
                    config_.input_topic, config_.num_partitions,
                    [&]() {
                        std::string levels;
                        for (size_t i = 0; i < config_.depth_levels.size(); ++i) {
                            if (i > 0) levels += ",";
                            levels += std::to_string(config_.depth_levels[i]);
                        }
                        return levels;
                    }());
    }

    MarketDepthProcessor::~MarketDepthProcessor() {
        if (running_) {
            stop_processing();
        }
    }

    bool MarketDepthProcessor::initialize() {
        try {
            // Initialize Kafka consumer
            KafkaConsumer &consumer = KafkaConsumer::instance();
            consumer.initialize(config_.kafka_config_path);
            consumer.subscribe({config_.input_topic});

            // Initialize Kafka producer
            KafkaProducer &producer = KafkaProducer::instance();
            producer.initialize(config_.kafka_config_path);

            // Initialize message factory and router
            message_factory_ = std::make_unique<MessageFactory>(config_.json_config);
            message_router_ = std::make_unique<MessageRouter>(config_.topic_config);

            // Reset metrics
            metrics_.reset();

            SPDLOG_INFO("MarketDepthProcessor initialized successfully");
            return true;
        } catch (const std::exception &e) {
            SPDLOG_ERROR("Failed to initialize MarketDepthProcessor: {}", e.what());
            return false;
        }
    }

    void MarketDepthProcessor::start_processing(uint32_t max_runtime_s) {
        if (running_) {
            SPDLOG_WARN("Processor is already running");
            return;
        }

        running_ = true;
        should_stop_ = false;

        SPDLOG_INFO("Starting simplified market depth processor (max_runtime={}s)", max_runtime_s);

        // Start statistics thread if enabled
        if (config_.enable_statistics) {
            stats_thread_ = std::thread(&MarketDepthProcessor::stats_thread, this);
        }

        // Start main processing
        auto start_time = std::chrono::steady_clock::now();
        processing_loop();

        // Check if we should stop due to runtime limit
        if (max_runtime_s > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed >= max_runtime_s) {
                SPDLOG_INFO("Stopping processor after {}s (max_runtime reached)", elapsed);
                stop_processing();
            }
        }
    }

    void MarketDepthProcessor::stop_processing() {
        if (!running_) return;

        SPDLOG_INFO("Stopping simplified market depth processor...");
        should_stop_ = true;

        // Wait for threads to finish
        if (stats_thread_.joinable()) {
            stats_thread_.join();
        }

        running_ = false;

        // Print final statistics
        if (config_.enable_statistics) {
            print_statistics();
        }

        SPDLOG_INFO("Simplified market depth processor stopped");
    }

    void MarketDepthProcessor::processing_loop() {
        KafkaConsumer &consumer = KafkaConsumer::instance();

        while (!should_stop_) {
            // Poll for message from any partition
            rd_kafka_message_t *msg = consumer.consume(config_.consumer_poll_timeout_ms);

            if (!msg) {
                // No message available, continue polling
                continue;
            }

            if (msg->err) {
                if (msg->err != RD_KAFKA_RESP_ERR__PARTITION_EOF) {
                    SPDLOG_ERROR("Kafka consume error: {}", rd_kafka_err2str(msg->err));
                    metrics_.kafka_errors++;
                }
                rd_kafka_message_destroy(msg);
                continue;
            }

            // Process the message
            auto start_time = get_timestamp();
            bool success = process_message(msg);
            auto processing_time = get_timestamp() - start_time;

            // Update metrics
            metrics_.messages_consumed++;
            if (success) {
                metrics_.messages_processed++;
                metrics_.update_processing_time(processing_time);
            } else {
                metrics_.processing_errors++;
            }

            // Clean up
            rd_kafka_message_destroy(msg);

            // Check for periodic flush
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_flush_time_).count();

            if (elapsed_ms >= config_.flush_interval_ms) {
                rd_kafka_flush(KafkaProducer::instance().get_producer(), 100);
                last_flush_time_ = now;
            }
        }
    }

    bool MarketDepthProcessor::process_message(rd_kafka_message_t *msg) {
        if (!msg || !msg->payload || msg->len == 0) {
            SPDLOG_WARN("Received empty or invalid message");
            return false;
        }

        try {
            // Parse FlatBuffers message
            const uint8_t *data = static_cast<const uint8_t *>(msg->payload);

            // Get envelope
            const auto *envelope = fb::GetEnvelope(data);
            if (!envelope) {
                SPDLOG_ERROR("Failed to parse FlatBuffers envelope");
                return false;
            }

            // Check message type
            if (envelope->msg_type() != fb::BookMsg_OrderBookSnapshot) {
                SPDLOG_DEBUG("Ignoring non-snapshot message type: {}", envelope->msg_type());
                return true; // Not an error, just not what we're looking for
            }

            // Get snapshot
            const auto *snapshot = envelope->msg_as_OrderBookSnapshot();
            if (!snapshot) {
                SPDLOG_ERROR("Failed to get OrderBookSnapshot from envelope");
                return false;
            }

            // Process snapshot directly
            return process_snapshot(snapshot);

        } catch (const std::exception &e) {
            SPDLOG_ERROR("Exception processing message: {}", e.what());
            return false;
        }
    }

    bool MarketDepthProcessor::process_snapshot(const fb::OrderBookSnapshot* snapshot) {
        if (!snapshot || !snapshot->symbol()) {
            SPDLOG_ERROR("Invalid snapshot: null or missing symbol");
            return false;
        }

        std::string symbol = snapshot->symbol()->str();

        try {
            // Publish snapshots directly for all depth levels
            publish_snapshots(symbol, snapshot);

            // Update symbol-specific metrics
            metrics_.symbol_message_counts[symbol]++;

            SPDLOG_TRACE("Processed snapshot for symbol: {} (seq: {})", symbol, snapshot->seq());
            return true;

        } catch (const std::exception &e) {
            SPDLOG_ERROR("Failed to process snapshot for symbol {}: {}", symbol, e.what());
            return false;
        }
    }

    void MarketDepthProcessor::publish_snapshots(const std::string& symbol, const fb::OrderBookSnapshot* snapshot) {
        try {
            // Convert FlatBuffers snapshot to internal format for each depth level
            for (uint32_t depth : config_.depth_levels) {
                // Create internal snapshot structure
                InternalOrderBookSnapshot internal_snapshot;
                internal_snapshot.symbol = symbol;
                internal_snapshot.sequence = snapshot->seq();
                internal_snapshot.timestamp = get_timestamp();
                internal_snapshot.last_trade_price = snapshot->recent_trade_price();
                internal_snapshot.last_trade_quantity = snapshot->recent_trade_qty();

                // Convert bid levels (limited by depth)
                if (snapshot->buy_side()) {
                    uint32_t bid_count = 0;
                    for (uint32_t i = 0; i < snapshot->buy_side()->size() && bid_count < depth; ++i) {
                        const auto* fb_level = snapshot->buy_side()->Get(i);
                        if (fb_level) {
                            PriceLevel level = convert_price_level(fb_level);
                            if (level.price > 0 && level.quantity > 0) {
                                internal_snapshot.bid_levels[level.price] = level;
                                bid_count++;
                            }
                        }
                    }
                }

                // Convert ask levels (limited by depth)
                if (snapshot->sell_side()) {
                    uint32_t ask_count = 0;
                    for (uint32_t i = 0; i < snapshot->sell_side()->size() && ask_count < depth; ++i) {
                        const auto* fb_level = snapshot->sell_side()->Get(i);
                        if (fb_level) {
                            PriceLevel level = convert_price_level(fb_level);
                            if (level.price > 0 && level.quantity > 0) {
                                internal_snapshot.ask_levels[level.price] = level;
                                ask_count++;
                            }
                        }
                    }
                }

                // Only publish if we have sufficient data
                if (internal_snapshot.bid_levels.size() >= depth && internal_snapshot.ask_levels.size() >= depth) {
                    // Generate JSON for this depth level
                    std::string json_payload = message_factory_->create_snapshot_json(internal_snapshot, depth);

                    // Create topic name: market_depth.[SYMBOL_NAME]
                    std::string topic = "market_depth." + symbol;

                    // Use symbol for partitioning
                    uint32_t partition = message_router_->calculate_partition(symbol);

                    // Publish to Kafka
                    KafkaPush(topic, partition, json_payload.c_str(), json_payload.size());
                    metrics_.messages_published++;

                    SPDLOG_TRACE("Published depth {} for symbol {} to topic {} partition {}",
                                depth, symbol, topic, partition);
                } else {
                    SPDLOG_DEBUG("Insufficient depth for symbol {}: requested={}, available_bids={}, available_asks={}",
                                symbol, depth, internal_snapshot.bid_levels.size(), internal_snapshot.ask_levels.size());
                }
            }

        } catch (const std::exception &e) {
            SPDLOG_ERROR("Failed to publish snapshots for symbol {}: {}", symbol, e.what());
            metrics_.processing_errors++;
        }
    }

    PriceLevel MarketDepthProcessor::convert_price_level(const fb::OrderMsgLevel* fb_level) const {
        PriceLevel level;
        level.price = fb_level->price();
        level.quantity = 0;
        level.num_orders = 0;
        level.exchanges.push_back(config_.json_config.exchange_name);

        // Aggregate orders at this price level
        if (fb_level->orders()) {
            for (uint32_t j = 0; j < fb_level->orders()->size(); ++j) {
                const auto* order = fb_level->orders()->Get(j);
                if (order) {
                    level.quantity += order->qty();
                    level.num_orders++;
                }
            }
        }

        return level;
    }

    void MarketDepthProcessor::stats_thread() {
        while (!should_stop_) {
            std::this_thread::sleep_for(std::chrono::seconds(config_.stats_report_interval_s));

            if (!should_stop_) {
                print_statistics();
            }
        }
    }

    PerformanceMetrics MarketDepthProcessor::get_metrics() const {
        std::lock_guard lock(metrics_mutex_);
        return metrics_;
    }

    void MarketDepthProcessor::print_statistics() const {
        auto now = std::chrono::high_resolution_clock::now();
        auto total_runtime_s = std::chrono::duration_cast<std::chrono::seconds>(
            now - metrics_.start_time).count();

        uint64_t consumed = metrics_.messages_consumed.load();
        uint64_t processed = metrics_.messages_processed.load();
        uint64_t published = metrics_.messages_published.load();
        uint64_t errors = metrics_.processing_errors.load();
        uint64_t kafka_errors = metrics_.kafka_errors.load();

        uint64_t total_processing_time = metrics_.total_processing_time_us.load();
        uint64_t max_processing_time = metrics_.max_processing_time_us.load();
        uint64_t min_processing_time = metrics_.min_processing_time_us.load();

        double avg_processing_time_us = processed > 0 ? static_cast<double>(total_processing_time) / processed : 0.0;
        double msg_rate = total_runtime_s > 0 ? static_cast<double>(consumed) / total_runtime_s : 0.0;

        SPDLOG_INFO("=== SIMPLIFIED PROCESSOR STATISTICS ({}s runtime) ===", total_runtime_s);
        SPDLOG_INFO("Messages: consumed={}, processed={}, published={}", consumed, processed, published);
        SPDLOG_INFO("Errors: processing={}, kafka={}", errors, kafka_errors);
        SPDLOG_INFO("Rate: {:.1f} msg/s", msg_rate);
        SPDLOG_INFO("Processing time (μs): avg={:.1f}, min={}, max={}",
                    avg_processing_time_us, min_processing_time, max_processing_time);

        // Active symbols count
        SPDLOG_INFO("Active symbols: {}", metrics_.symbol_message_counts.size());

        // Top 10 symbols by message count
        std::vector<std::pair<std::string, uint64_t>> symbol_stats;
        for (const auto& [symbol, count] : metrics_.symbol_message_counts) {
            symbol_stats.emplace_back(symbol, count.load());
        }

        std::sort(symbol_stats.begin(), symbol_stats.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        SPDLOG_INFO("Top symbols by message count:");
        for (size_t i = 0; i < std::min(symbol_stats.size(), size_t(10)); ++i) {
            SPDLOG_INFO("  {}: {}", symbol_stats[i].first, symbol_stats[i].second);
        }
    }

    // ProcessorShutdownHandler Implementation
    ProcessorShutdownHandler *ProcessorShutdownHandler::instance_ = nullptr;

    ProcessorShutdownHandler::ProcessorShutdownHandler(MarketDepthProcessor &processor)
        : processor_(processor) {
        instance_ = this;
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
    }

    ProcessorShutdownHandler::~ProcessorShutdownHandler() {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        instance_ = nullptr;
    }

    void ProcessorShutdownHandler::signal_handler(int signal) {
        SPDLOG_INFO("Received signal {}, initiating shutdown...", signal);
        if (instance_ && instance_->processor_.is_running()) {
            instance_->processor_.stop_processing();
        }
    }

} // namespace market_depth