/**
 * @file    KafkaConsumer.cpp
 * @brief   Kafka consumer singleton implementation using librdkafka.
 *
 * Developer: [Your Name]
 * Copyright: [Your Company] ([Your Email])
 * Filename: KafkaConsumer.cpp
 * Created: [Current Date]
 *
 * Description:
 *   Implementation of KafkaConsumer singleton for managing Kafka configuration,
 *   topic subscription, and message consumption. Handles config loading from YAML,
 *   thread-safe consumption, and clean shutdown.
 */

#include "KafkaConsumer.hpp"

#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <iostream>

KafkaConsumer& KafkaConsumer::instance() {
    static KafkaConsumer instance;
    return instance;
}

KafkaConsumer::KafkaConsumer()
    : consumer_(nullptr), initialized_(false) {}

KafkaConsumer::~KafkaConsumer() {
    shutdown();
}

void KafkaConsumer::initialize(const std::string& config_path) {
    if (initialized_) return; // Already initialized
    parse_config(config_path);

    char errstr[512];
    rd_kafka_conf_t* conf = rd_kafka_conf_new();

    // Required: bootstrap servers and group.id
    if (rd_kafka_conf_set(conf, "bootstrap.servers", bootstrap_servers_.c_str(), errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
        throw std::runtime_error("Failed to set bootstrap.servers: " + std::string(errstr));
    if (rd_kafka_conf_set(conf, "group.id", group_id_.c_str(), errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
        throw std::runtime_error("Failed to set group.id: " + std::string(errstr));

    rd_kafka_conf_set(conf, "session.timeout.ms", session_timeout_ms_.c_str(), errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "auto.offset.reset", auto_offset_reset_.c_str(), errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "enable.auto.commit", enable_auto_commit_.c_str(), errstr, sizeof(errstr));

    consumer_ = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    if (!consumer_)
        throw std::runtime_error("Failed to create Kafka consumer: " + std::string(errstr));

    rd_kafka_poll_set_consumer(consumer_); // Required for consumer

    initialized_ = true;
    SPDLOG_INFO("KafkaConsumer initialized");
}

void KafkaConsumer::parse_config(const std::string& config_path) {
    SPDLOG_INFO("KafkaConsumer loading config file: {}", config_path);
    YAML::Node config = YAML::LoadFile(config_path);

    auto kafka = config["kafka_consumer"];
    if (!kafka)
        throw std::runtime_error("KafkaConsumer config: missing 'kafka_cluster' node");

    bootstrap_servers_   = kafka["bootstrap_servers"] ? kafka["bootstrap_servers"].as<std::string>() : "localhost:9092";
    group_id_            = kafka["group_id"]          ? kafka["group_id"].as<std::string>()          : "default-group";
    session_timeout_ms_  = kafka["session_timeout_ms"]? std::to_string(kafka["session_timeout_ms"].as<int>()) : "6000";
    auto_offset_reset_   = kafka["auto_offset_reset"] ? kafka["auto_offset_reset"].as<std::string>() : "earliest";
    enable_auto_commit_  = kafka["enable_auto_commit"]? kafka["enable_auto_commit"].as<bool>() ? "true" : "false" : "true";
}

void KafkaConsumer::subscribe(const std::vector<std::string>& topics) {
    std::unique_lock lock(consumer_mutex_);

    if (!consumer_)
        throw std::runtime_error("KafkaConsumer::subscribe: Consumer not initialized");

    rd_kafka_topic_partition_list_t* topic_list = rd_kafka_topic_partition_list_new(static_cast<int>(topics.size()));
    for (const auto& topic : topics) {
        rd_kafka_topic_partition_list_add(topic_list, topic.c_str(), -1);
        subscribed_topics_.insert(topic);
    }
    int err = rd_kafka_subscribe(consumer_, topic_list);
    rd_kafka_topic_partition_list_destroy(topic_list);

    if (err)
        throw std::runtime_error("KafkaConsumer::subscribe: Failed to subscribe to topics");

    SPDLOG_INFO("KafkaConsumer subscribed to {} topics", topics.size());
}

rd_kafka_message_t* KafkaConsumer::consume(int timeout_ms) {
    std::shared_lock lock(consumer_mutex_);
    if (!consumer_)
        return nullptr;

    rd_kafka_message_t* msg = rd_kafka_consumer_poll(consumer_, timeout_ms);
    return msg; // msg is managed by caller (must call rd_kafka_message_destroy)
}

void KafkaConsumer::shutdown() {
    std::unique_lock lock(consumer_mutex_);
    if (consumer_) {
        SPDLOG_INFO("KafkaConsumer flush and close");
        rd_kafka_consumer_close(consumer_);
        rd_kafka_destroy(consumer_);
        consumer_ = nullptr;
    }
    initialized_ = false;
}

rd_kafka_t* KafkaConsumer::get_consumer() {
    std::shared_lock lock(consumer_mutex_);
    return consumer_;
}