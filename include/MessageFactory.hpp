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

namespace market_depth {

/**
 * @brief JSON message factory for market depth data
 */
class MessageFactory {
public:
    /**
     * @brief Configuration for JSON formatting
     */
    struct JsonConfig {
        uint32_t price_decimals;
        uint32_t quantity_decimals;
        bool include_timestamp;
        bool include_sequence;
        bool compact_format;
        std::string exchange_name;

        JsonConfig();
    };

    explicit MessageFactory(const JsonConfig& config);
    MessageFactory();

    std::string create_snapshot_json(const InternalOrderBookSnapshot& snapshot, uint32_t depth) const;
    std::string create_cdc_json(const CDCEvent& event) const;
    std::map<uint32_t, std::string> create_multi_depth_json(
        const InternalOrderBookSnapshot& snapshot,
        const std::vector<uint32_t>& depth_levels) const;

    void update_config(const JsonConfig& config) { config_ = config; }
    const JsonConfig& get_config() const { return config_; }

private:
    std::string format_price(uint64_t price_scaled) const;
    std::string format_quantity(uint64_t quantity_scaled) const;
    nlohmann::json price_level_to_json(const PriceLevel& level, OrderSide side,
                                      const std::string& symbol) const;
    void add_common_fields(nlohmann::json& j, const std::string& symbol,
                          uint64_t sequence, uint64_t timestamp) const;

    static std::string side_to_string(OrderSide side);
    static std::string cdc_event_type_to_string(CDCEventType type);

private:
    JsonConfig config_;
};

/**
 * @brief Kafka message wrapper with topic routing information
 */
struct KafkaMessage {
    std::string topic;
    std::string key;
    std::string payload;
    uint32_t partition;

    KafkaMessage(const std::string& t, const std::string& k, const std::string& p, uint32_t part = static_cast<uint32_t>(-1));
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
        std::string snapshot_topic_prefix;
        std::string cdc_topic;
        bool use_depth_in_topic;
        bool use_symbol_partitioning;
        uint32_t num_partitions;

        TopicConfig();
    };

    explicit MessageRouter(const TopicConfig& config);
    MessageRouter();

    KafkaMessage route_snapshot(const std::string& symbol, uint32_t depth,
                               const std::string& json_payload) const;
    KafkaMessage route_cdc(const std::string& symbol, const std::string& json_payload) const;
    uint32_t calculate_partition(const std::string& symbol) const;
    void update_config(const TopicConfig& config) { config_ = config; }

private:
    TopicConfig config_;
    std::hash<std::string> hasher_;
};

} // namespace market_depth

#endif /* MESSAGE_FACTORY_HPP_ */