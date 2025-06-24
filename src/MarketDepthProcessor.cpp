/**
 * @file    MarketDepthProcessor.cpp
 * @brief   Main market depth processor implementation
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
          , max_processing_threads(4)
          , max_messages_per_batch(1000)
          , flush_interval_ms(1000)
          , enable_statistics(true)
          , stats_report_interval_s(30)
          , use_symbol_threading(true)
          , message_queue_size(10000)
          , enable_back_pressure(true) {
    }

    MarketDepthProcessor::MarketDepthProcessor(const ProcessorConfig &config)
        : config_(config)
          , running_(false)
          , should_stop_(false)
          , last_flush_time_(std::chrono::high_resolution_clock::now()) {
        SPDLOG_INFO("MarketDepthProcessor created with config: input_topic={}, max_threads={}, enable_cdc={}",
                    config_.input_topic, config_.max_processing_threads, config_.depth_config.enable_cdc);
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

            // Initialize order book manager with CDC callback
            auto cdc_callback = [this](const CDCEvent &event) {
                this->on_cdc_event(event);
            };
            orderbook_manager_ = std::make_unique<OrderBookManager>(config_.depth_config, cdc_callback);

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

        SPDLOG_INFO("Starting market depth processor (max_runtime={}s)", max_runtime_s);

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

        SPDLOG_INFO("Stopping market depth processor...");
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

        SPDLOG_INFO("Market depth processor stopped");
    }

    void MarketDepthProcessor::processing_loop() {
        KafkaConsumer &consumer = KafkaConsumer::instance();

        while (!should_stop_) {
            // Poll for message
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
                // SPDLOG_DEBUG("Ignoring non-snapshot message type: {}", envelope->msg_type());
                return true; // Not an error, just not what we're looking for
            }

            // Get snapshot
            const auto *snapshot = envelope->msg_as_OrderBookSnapshot();
            if (!snapshot) {
                SPDLOG_ERROR("Failed to get OrderBookSnapshot from envelope");
                return false;
            }

            // Process snapshot through order book manager
            bool success = orderbook_manager_->process_snapshot(snapshot);

            if (success && config_.depth_config.enable_snapshots) {
                // Get the updated order book snapshot
                if (snapshot->symbol()) {
                    SPDLOG_INFO("Market depth processing succeeded: {}", snapshot->symbol()->str());
                    std::string symbol = snapshot->symbol()->str();
                    // publish_snapshots(snapshot);
                    OrderBook *orderbook = orderbook_manager_->get_or_create_orderbook(symbol);

                    if (orderbook && orderbook->is_initialized()) {
                        publish_snapshots(orderbook->get_snapshot());
                    }
                }
            }

            return success;
        } catch (const std::exception &e) {
            SPDLOG_ERROR("Exception processing message: {}", e.what());
            return false;
        }
    }

    void MarketDepthProcessor::on_cdc_event(const CDCEvent &event) {
        if (config_.depth_config.enable_cdc) {
            publish_cdc_event(event);
            metrics_.symbol_message_counts[event.symbol]++;
        }
    }

    void MarketDepthProcessor::publish_snapshots(const InternalOrderBookSnapshot &snapshot) {
        try {
            // Generate JSON for all configured depth levels
            auto depth_messages = message_factory_->create_multi_depth_json(
                snapshot, config_.depth_config.depth_levels);

            for (const auto &[depth, json_payload]: depth_messages) {
                // Route message to appropriate topic/partition
                auto kafka_msg = message_router_->route_snapshot(
                    snapshot.symbol, depth, json_payload);

                // Publish to Kafka
                KafkaPush(kafka_msg.topic, kafka_msg.partition,
                          kafka_msg.payload.c_str(), kafka_msg.payload.size());

                metrics_.messages_published++;
            }
        } catch (const std::exception &e) {
            SPDLOG_ERROR("Failed to publish snapshots for symbol {}: {}",
                         snapshot.symbol, e.what());
            metrics_.processing_errors++;
        }
    }

    void MarketDepthProcessor::publish_cdc_event(const CDCEvent &event) {
        try {
            // Generate CDC JSON
            std::string json_payload = message_factory_->create_cdc_json(event);

            // Route message
            auto kafka_msg = message_router_->route_cdc(event.symbol, json_payload);

            // Publish to Kafka
            KafkaPush(kafka_msg.topic, kafka_msg.partition,
                      kafka_msg.payload.c_str(), kafka_msg.payload.size());

            metrics_.messages_published++;
        } catch (const std::exception &e) {
            SPDLOG_ERROR("Failed to publish CDC event for symbol {}: {}",
                         event.symbol, e.what());
            metrics_.processing_errors++;
        }
    }

    void MarketDepthProcessor::stats_thread() {
        while (!should_stop_) {
            std::this_thread::sleep_for(std::chrono::seconds(config_.stats_report_interval_s));

            if (!should_stop_) {
                print_statistics();
            }
        }
    }

    //
    // PerformanceMetrics MarketDepthProcessor::get_metrics() const {
    //     std::lock_guard lock(metrics_mutex_);
    //     return metrics_;
    // }

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

        SPDLOG_INFO("=== PERFORMANCE STATISTICS ({}s runtime) ===", total_runtime_s);
        SPDLOG_INFO("Messages: consumed={}, processed={}, published={}", consumed, processed, published);
        SPDLOG_INFO("Errors: processing={}, kafka={}", errors, kafka_errors);
        SPDLOG_INFO("Rate: {:.1f} msg/s", msg_rate);
        SPDLOG_INFO("Processing time (μs): avg={:.1f}, min={}, max={}",
                    avg_processing_time_us, min_processing_time, max_processing_time);

        // Top symbols by message count
        SPDLOG_INFO("Active symbols: {}", orderbook_manager_->get_tracked_symbols().size());

        auto aggregate_stats = orderbook_manager_->get_aggregate_stats();
        SPDLOG_INFO("Order book stats: symbols={}, total_processed={}",
                    aggregate_stats.symbol_message_counts.size(),
                    aggregate_stats.messages_processed);
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
