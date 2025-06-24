/**
 * @file    MessageFactory.hpp
 * @brief   JSON message factory for market depth data
 *
 * Developer: Equix Technologies
 * Copyright: Equix Technologies Pty Ltd
 * Created: June 2025
 *
 * Description:
 *   Converts internal order book structures to JSON format for downstream
 *   consumers. Supports both full snapshots and CDC events with configurable
 *   depth levels and formatting options.
 */

#pragma once

#ifndef MESSAGE_FACTORY_HPP_
#define MESSAGE_FACTORY_HPP_

#include "OrderBookTypes.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>
#include <iomanip>

namespace md {

/**
 * @brief JSON message factory for market depth data
 */
class MessageFactory {
public:
    /**
     * @brief Configuration for JSON formatting
     */
    struct JsonConfig {
        uint32_t price_decimals = 4;      // Decimal places for price formatting
        uint32_t quantity_decimals = 2;   // Decimal places for quantity formatting
        bool include_timestamp = true;    // Include timestamp in messages
        bool include_sequence = true;     // Include sequence numbers
        bool compact_format = false;      // Use compact JSON (no pretty printing)
        std::string exchange_name = "CBOE"; // Default exchange name
    };

    explicit MessageFactory(const JsonConfig& config = JsonConfig{});

    /**
     * @brief Create JSON snapshot message for specific depth
     * @param snapshot Order book snapshot
     * @param depth Number of price levels to include (5, 10, 25, 50)
     * @return JSON string
     */
    std::string create_snapshot_json(const OrderBookSnapshot& snapshot, uint32_t depth) const;

    /**
     * @brief Create JSON CDC event message
     * @param event CDC event
     * @return JSON string
     */
    std::string create_cdc_json(const CDCEvent& event) const;

    /**
     * @brief Create full market depth JSON (all depth levels)
     * @param snapshot Order book snapshot
     * @param depth_levels Vector of depth levels to generate
     * @return Map of depth -> JSON string
     */
    std::map<uint32_t, std::string> create_multi_depth_json(
        const OrderBookSnapshot& snapshot,
        const std::vector<uint32_t>& depth_levels) const;

    /**
     * @brief Update configuration
     */
    void update_config(const JsonConfig& config) { config_ = config; }

    /**
     * @brief Get current configuration
     */
    const JsonConfig& get_config() const { return config_; }

private:
    /**
     * @brief Format price as string with proper decimal places
     */
    std::string format_price(uint64_t price_scaled) const;

    /**
     * @brief Format quantity as string with proper decimal places
     */
    std::string format_quantity(uint64_t quantity_scaled) const;

    /**
     * @brief Convert price level to JSON object
     */
    nlohmann::json price_level_to_json(const PriceLevel& level, OrderSide side,
                                      const std::string& symbol) const;

    /**
     * @brief Add common fields to JSON message
     */
    void add_common_fields(nlohmann::json& j, const std::string& symbol,
                          uint64_t sequence, uint64_t timestamp_us) const;

    /**
     * @brief Convert OrderSide enum to string
     */
    static std::string side_to_string(OrderSide side);

    /**
     * @brief Convert CDCEventType enum to string
     */
    static std::string cdc_event_type_to_string(CDCEventType type);

private:
    JsonConfig config_;
};

/**
 * @brief Kafka message wrapper with topic routing information
 */
struct KafkaMessage {
    std::string topic;
    std::string key;        // Usually symbol for partitioning
    std::string payload;    // JSON payload
    uint32_t partition;     // Kafka partition (optional, -1 for auto)

    KafkaMessage(const std::string& t, const std::string& k, const std::string& p, uint32_t part = -1)
        : topic(t), key(k), payload(p), partition(part) {}
};

/**
 * @brief Message router for determining Kafka topics and partitioning
 */
class MessageRouter {
public:
    /**
     * @brief Topic configuration
     */
    struct TopicConfig {
        std::string snapshot_topic_prefix = "market_depth_snapshot_";
        std::string cdc_topic = "market_depth_cdc";
        bool use_depth_in_topic = true;   // e.g., market_depth_snapshot_5
        bool use_symbol_partitioning = true;
        uint32_t num_partitions = 16;
    };

    explicit MessageRouter(const TopicConfig& config = TopicConfig{});

    /**
     * @brief Route snapshot message to appropriate Kafka topic/partition
     */
    KafkaMessage route_snapshot(const std::string& symbol, uint32_t depth,
                               const std::string& json_payload) const;

    /**
     * @brief Route CDC message to appropriate Kafka topic/partition
     */
    KafkaMessage route_cdc(const std::string& symbol, const std::string& json_payload) const;

    /**
     * @brief Calculate partition for symbol (consistent hashing)
     */
    uint32_t calculate_partition(const std::string& symbol) const;

    /**
     * @brief Update configuration
     */
    void update_config(const TopicConfig& config) { config_ = config; }

private:
    TopicConfig config_;
    std::hash<std::string> hasher_;
};

} // namespace md

#endif /* MESSAGE_FACTORY_HPP_ */