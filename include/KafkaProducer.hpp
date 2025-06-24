/**
 * @file    KafkaProducer.hpp
 * @brief   Singleton wrapper for producing messages to Kafka using librdkafka.
 *
 * Developer: Hoang Nguyen & Tan A. Pham
 * Copyright: Equix Technologies Pty Ltd (contact@equix.com.au)
 * Filename: KafkaProducer.hpp
 * Created: 28/May/2025
 *
 * Description:
 *   Provides configuration management, topic management, and producer interface for Kafka.
 *   Supports config loading from YAML, topic preallocation, and clean shutdown.
 */

#pragma once

#ifndef KAFKA_PRODUCER_HPP_
#define KAFKA_PRODUCER_HPP_

#include <librdkafka/rdkafka.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>

/**
 * @class KafkaProducer
 * @brief Singleton for producing to Kafka, managing configuration and topic handles.
 */
class KafkaProducer {
public:
    /**
     * @brief Returns the singleton instance of KafkaProducer.
     */
    static KafkaProducer& instance();

    /**
     * @brief Initializes producer and loads configuration from YAML.
     * @param config_path Path to the YAML config file.
     * @throws std::runtime_error on error (bad YAML, missing fields, etc.)
     */
    void initialize(const std::string& config_path);

    /**
     * @brief Returns the underlying librdkafka producer handle.
     */
    rd_kafka_t* get_producer();

    /**
     * @brief Gets the topic handle for the given symbol/topic name.
     * @param symbol Kafka topic name (e.g., symbol).
     */
    rd_kafka_topic_t* get_topic(const std::string& topic_name) const;

    /**
     * @brief Preallocates topic handles for all topic names you expect to use.
     * @param topic_names Vector of topic/symbol names.
     */
    void preallocate_topics(const std::vector<std::string>& topic_names);

    /**
     * @brief Clean shutdown, flush, and resource release. Call before program exit.
     */
    void shutdown();

    /**
     * @brief Returns the list of topics configured from YAML.
     */
    const std::vector<std::string>& topic_list() const { return topics_; }

    /**
     * @brief Returns the topic handle for the given topic, creating it if not already in cache.
     *
     * @param topic_name Name of the topic.
     * @return Pointer to rd_kafka_topic_t* handle (newly created or cached).
     * @note Thread-safe.
     */
    rd_kafka_topic_t* get_or_create_topic(const std::string& topic_name);

    /* Prevent copy/move. */
    KafkaProducer(const KafkaProducer&) = delete;               /* Deleted copy constructor. */
    KafkaProducer& operator=(const KafkaProducer&) = delete;    /* Deleted copy assignment. */

private:
    KafkaProducer();
    ~KafkaProducer();

    /**
     * @brief Parses YAML config and sets Kafka producer configuration.
     *
     * @param config_path Path to the YAML config file.
     * @throws std::runtime_error on configuration errors.
     */
    void parse_config(const std::string& config_path);

    /* Config loaded from YAML or other source. */
    std::string bootstrap_servers_;        /* Kafka bootstrap servers (comma-separated). */
    std::string compression_;              /* Compression codec (e.g. "snappy"). */
    std::string acks_;                     /* Number of acks to require from broker. */
    std::string queue_buffering_max_messages_;
    std::string batch_num_messages_;
    std::string linger_ms_;
    std::vector<std::string> topics_;      /* List of topics (symbols) loaded from config. */

    rd_kafka_t* producer_;                                        /* Underlying librdkafka producer. */
    std::unordered_map<std::string, rd_kafka_topic_t*> topic_cache_; /* Cache of topic handles by topic name. */
    mutable std::shared_mutex topic_cache_mutex_;                 /* Mutex for thread-safe topic cache access. */
    bool initialized_;                                            /* Initialization status. */
};

#endif /* KAFKA_PRODUCER_HPP_ */
