/**
 * @file    KafkaProducer.cpp
 * @brief   Kafka producer singleton implementation using librdkafka.
 *
 * Developer: Hoang Nguyen & Tan A. Pham
 * Copyright: Equix Technologies Pty Ltd (contact@equix.com.au)
 * Filename: KafkaProducer.cpp
 * Created: 28/May/2025
 *
 * Description:
 *   Implementation of KafkaProducer singleton for managing Kafka configuration,
 *   producer lifecycle, and topic handles. Handles config loading from YAML,
 *   thread-safe topic handle caching, and clean shutdown.
 */

#include "KafkaProducer.hpp"
#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <iostream>
#include "spdlog/spdlog.h"

/**
 * @brief Returns the global singleton instance of KafkaProducer.
 */
KafkaProducer& KafkaProducer::instance() {
    static KafkaProducer instance;
    return instance;
}

/**
 * @brief Constructs a KafkaProducer. Members are initialized to safe defaults.
 */
KafkaProducer::KafkaProducer()
    : producer_(nullptr), initialized_(false) {}

/**
 * @brief Destructor. Ensures all resources are released and the producer is properly shut down.
 */
KafkaProducer::~KafkaProducer() {
    shutdown();
}

/**
 * @brief Initializes the Kafka producer and loads configuration from YAML file.
 *        This method is idempotent: calling it more than once has no effect after initialization.
 * @param config_path Path to the YAML configuration file.
 * @throws std::runtime_error on configuration or producer creation errors.
 */
void KafkaProducer::initialize(const std::string& config_path) {
    if (initialized_) return; // Already initialized; do nothing

    // Parse configuration from YAML file
    parse_config(config_path);

    // Create and configure librdkafka producer
    char errstr[512];
    rd_kafka_conf_t* conf = rd_kafka_conf_new();

    // Set all required Kafka config parameters from YAML
    rd_kafka_conf_set(conf, "bootstrap.servers", bootstrap_servers_.c_str(), errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "queue.buffering.max.messages", queue_buffering_max_messages_.c_str(), errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "batch.num.messages", batch_num_messages_.c_str(), errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "linger.ms", linger_ms_.c_str(), errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "compression.type", compression_.c_str(), errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "acks", acks_.c_str(), errstr, sizeof(errstr));

    // Instantiate the producer handle
    producer_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (!producer_) {
        // Clean up and throw on error
        throw std::runtime_error("Failed to create Kafka producer: " + std::string(errstr));
    }

    initialized_ = true; // Mark as initialized to prevent re-init
}

/**
 * @brief Loads and parses the Kafka configuration from a YAML file.
 * @param config_path Path to the YAML configuration file.
 * @throws std::runtime_error if required fields are missing.
 */
void KafkaProducer::parse_config(const std::string& config_path) {

    SPDLOG_INFO("Parse_config: Reading file {}", config_path);

    YAML::Node config = YAML::LoadFile(config_path);

    // Ensure required section exists
    if (!config["kafka_cluster"])
        throw std::runtime_error("Missing required 'kafka_cluster' section in config.yaml");
    auto kafka_config = config["kafka_cluster"];

    // Extract Kafka broker/server info and producer tuning parameters with defaults
    bootstrap_servers_ = kafka_config["bootstrap_servers"] ? kafka_config["bootstrap_servers"].as<std::string>() : "localhost:9092";
    compression_ = kafka_config["compression"] ? kafka_config["compression"].as<std::string>() : "lz4";
    acks_ = kafka_config["acks"] ? kafka_config["acks"].as<std::string>() : "1";
    queue_buffering_max_messages_ = kafka_config["queue_buffering_max_messages"] ? std::to_string(kafka_config["queue_buffering_max_messages"].as<int>()) : "1000000";
    batch_num_messages_ = kafka_config["batch_num_messages"] ? std::to_string(kafka_config["batch_num_messages"].as<int>()) : "10000";
    linger_ms_ = kafka_config["linger_ms"] ? std::to_string(kafka_config["linger_ms"].as<int>()) : "5";

    // Extract topic list from YAML
    topics_.clear();
    if (kafka_config["topics"]) {
        for (const auto& topic_node : kafka_config["topics"]) {
            topics_.push_back(topic_node.as<std::string>());
        }
    }

    SPDLOG_INFO("Parse_config: bootstrap_servers={} compression={}", bootstrap_servers_, compression_);
}

/**
 * @brief Returns the underlying librdkafka producer handle.
 * @return Pointer to the producer instance, or nullptr if not initialized.
 */
rd_kafka_t* KafkaProducer::get_producer() {
    return producer_;
}

/**
 * @brief Gets a thread-safe handle to a Kafka topic by name.
 * @param topic_name Name of the Kafka topic.
 * @return Pointer to the topic handle if found, otherwise nullptr.
 *
 * Thread-safe: shared lock allows concurrent lookups.
 */
rd_kafka_topic_t* KafkaProducer::get_topic(const std::string& topic_name) const {
    std::shared_lock lock(topic_cache_mutex_);
    auto it = topic_cache_.find(topic_name);
    if (it == topic_cache_.end()) return nullptr;
    return it->second;
}

/**
 * @brief Pre-creates topic handles for a vector of topic names (symbols).
 *        Thread-safe. Skips topics already in the cache.
 * @param topic_names Vector of topic names to preallocate.
 */
void KafkaProducer::preallocate_topics(const std::vector<std::string>& topic_names) {
    std::unique_lock lock(topic_cache_mutex_);
    for (const auto& topic_name : topic_names) {
        // Only allocate if not already cached
        if (topic_cache_.find(topic_name) == topic_cache_.end()) {
            rd_kafka_topic_t* topic = rd_kafka_topic_new(producer_, topic_name.c_str(), nullptr);
            if (!topic) {
                SPDLOG_ERROR("Failed to create topic handle: {}", topic_name);
                continue; // Skip failed topic
            }
            topic_cache_.emplace(topic_name, topic);
        }
    }
}

/**
 * @brief Flushes all outstanding messages, destroys all topic handles, and
 *        closes and destroys the producer. Thread-safe.
 *
 * Should be called before application exit to prevent message loss.
 */
void KafkaProducer::shutdown() {
    // Destroy all topic handles safely
    {
    SPDLOG_INFO("KafkaProducer Shutdown: Flushing and destroying producer and all topic handles");
        std::unique_lock lock(topic_cache_mutex_);
        for (auto& kv : topic_cache_) {
            if (kv.second)
                rd_kafka_topic_destroy(kv.second); // Release each topic handle
        }
        topic_cache_.clear(); // Remove all entries
    }
    // Flush and destroy the producer
    if (producer_) {
        rd_kafka_flush(producer_, 10000); // Wait up to 10s for message delivery
        rd_kafka_destroy(producer_);
        producer_ = nullptr;
    }
    initialized_ = false; // Allow future re-initialization if needed

    SPDLOG_INFO("KafkaProducer Shutdown: Done");
}

/**
 * @brief   Gets the topic handle for a given topic, or creates and caches it if it doesn't yet exist.
 *
 *          This provides a thread-safe mechanism to obtain a Kafka topic handle. If the handle for the
 *          specified topic name is already present in the cache, it is returned immediately; otherwise,
 *          a new topic handle is created and inserted into the cache.
 *
 * @param   topic_name  The name of the Kafka topic for which to get or create a handle.
 * @return  Pointer to the rd_kafka_topic_t handle for the topic, or nullptr if topic creation failed.
 *
 * @note    Thread-safe. Ensures only one topic handle is created per topic even with concurrent calls.
 */
rd_kafka_topic_t* KafkaProducer::get_or_create_topic(const std::string& topic_name) {
    {
        std::shared_lock lock(topic_cache_mutex_);
        auto it = topic_cache_.find(topic_name);
        if (it != topic_cache_.end()) return it->second;
    }
    std::unique_lock lock(topic_cache_mutex_);
    auto it = topic_cache_.find(topic_name);
    if (it != topic_cache_.end()) return it->second;
    rd_kafka_topic_t* topic = rd_kafka_topic_new(producer_, topic_name.c_str(), nullptr);
    if (!topic) {
        SPDLOG_ERROR("Failed to create topic handle: {}", topic_name);
        return nullptr;
    }
    topic_cache_.emplace(topic_name, topic);
    SPDLOG_DEBUG("Created handle for topic: {}", topic_name);
    return topic;
}
