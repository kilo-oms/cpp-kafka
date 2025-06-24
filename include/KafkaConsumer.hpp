/**
 * @file    KafkaConsumer.hpp
 * @brief   Singleton wrapper for consuming messages from Kafka using librdkafka.
 *
 * Developer: [Your Name]
 * Copyright: [Your Company] ([Your Email])
 * Filename: KafkaConsumer.hpp
 * Created: [Current Date]
 *
 * Description:
 *   Provides configuration management, topic subscription, and consumer interface for Kafka.
 *   Supports config loading from YAML, rebalance callback, thread-safe consumption, and clean shutdown.
 */

#pragma once

#ifndef KAFKA_CONSUMER_HPP_
#define KAFKA_CONSUMER_HPP_

#include <librdkafka/rdkafka.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>

/**
 * @class KafkaConsumer
 * @brief Singleton for consuming from Kafka, managing configuration, and subscriptions.
 */
class KafkaConsumer {
public:
    /**
     * @brief Returns the singleton instance of KafkaConsumer.
     */
    static KafkaConsumer& instance();

    /**
     * @brief Initializes consumer and loads configuration from YAML.
     * @param config_path Path to the YAML config file.
     * @throws std::runtime_error on error (bad YAML, missing fields, etc.)
     */
    void initialize(const std::string& config_path);

    /**
     * @brief Subscribes to a vector of topics.
     * @param topics Vector of topic names to subscribe to.
     */
    void subscribe(const std::vector<std::string>& topics);

    /**
     * @brief Polls a message from Kafka with a timeout (ms).
     * @param timeout_ms Poll timeout in milliseconds.
     * @return Pointer to rd_kafka_message_t, or nullptr if no message.
     *         Caller is responsible for rd_kafka_message_destroy().
     */
    rd_kafka_message_t* consume(int timeout_ms = 100);

    /**
     * @brief Clean shutdown and resource release.
     */
    void shutdown();

    /**
     * @brief Returns the consumer handle.
     */
    rd_kafka_t* get_consumer();

    /* Prevent copy/move */
    KafkaConsumer(const KafkaConsumer&) = delete;
    KafkaConsumer& operator=(const KafkaConsumer&) = delete;

private:
    KafkaConsumer();
    ~KafkaConsumer();

    /**
     * @brief Parses YAML config and sets Kafka consumer configuration.
     */
    void parse_config(const std::string& config_path);

    /* YAML-derived config */
    std::string bootstrap_servers_;
    std::string group_id_;
    std::string session_timeout_ms_;
    std::string auto_offset_reset_;
    std::string enable_auto_commit_;
    std::unordered_set<std::string> subscribed_topics_;

    rd_kafka_t* consumer_;
    mutable std::shared_mutex consumer_mutex_;
    bool initialized_;
};

#endif /* KAFKA_CONSUMER_HPP_ */