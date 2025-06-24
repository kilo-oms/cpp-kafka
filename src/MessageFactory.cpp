/**
 * @file    MessageFactory.cpp
 * @brief   Simplified JSON message factory implementation - snapshots only
 */

#include "MessageFactory.hpp"
#include "spdlog/spdlog.h"
#include <chrono>
#include <cmath>

namespace market_depth {

    // JsonConfig implementation
    MessageFactory::JsonConfig::JsonConfig()
        : price_decimals(4)
          , quantity_decimals(2)
          , include_timestamp(true)
          , include_sequence(true)
          , compact_format(false)
          , exchange_name("CXA") {
    }

    // MessageFactory implementation
    MessageFactory::MessageFactory(const JsonConfig &config) : config_(config) {
        SPDLOG_DEBUG("MessageFactory created with price_decimals={}, quantity_decimals={}",
                     config_.price_decimals, config_.quantity_decimals);
    }

    MessageFactory::MessageFactory() : config_() {
    }

    std::string MessageFactory::create_snapshot_json(const InternalOrderBookSnapshot &snapshot,
                                                     uint32_t depth) const {
        nlohmann::json j;

        // Add common fields
        add_common_fields(j, snapshot.symbol, snapshot.sequence, snapshot.timestamp);

        j["message_type"] = "snapshot";
        j["depth"] = depth;

        // Add bid side (top depth levels)
        nlohmann::json bids = nlohmann::json::array();
        auto top_bids = snapshot.get_top_bids(depth);
        for (const auto &level: top_bids) {
            bids.push_back(price_level_to_json(level, OrderSide::Buy, snapshot.symbol));
        }
        j["bids"] = bids;

        // Add ask side (top depth levels)
        nlohmann::json asks = nlohmann::json::array();
        auto top_asks = snapshot.get_top_asks(depth);
        for (const auto &level: top_asks) {
            asks.push_back(price_level_to_json(level, OrderSide::Sell, snapshot.symbol));
        }
        j["asks"] = asks;

        // Add trade info if available
        if (snapshot.last_trade_price > 0) {
            j["last_trade"] = {
                {"price", format_price(snapshot.last_trade_price)},
                {"quantity", format_quantity(snapshot.last_trade_quantity)}
            };
        }

        // Add market stats
        j["market_stats"] = {
            {"total_bid_levels", snapshot.bid_levels.size()},
            {"total_ask_levels", snapshot.ask_levels.size()},
            {"has_sufficient_depth", snapshot.has_sufficient_depth(depth)}
        };

        if (!top_bids.empty() && !top_asks.empty()) {
            j["market_stats"]["spread"] = format_price(top_asks[0].price - top_bids[0].price);
            j["market_stats"]["mid_price"] = format_price((top_asks[0].price + top_bids[0].price) / 2);
        }

        return config_.compact_format ? j.dump() : j.dump(2);
    }

    // CDC functionality removed - not needed in simplified version
    std::string MessageFactory::create_cdc_json(const CDCEvent &event) const {
        // This function is kept for interface compatibility but should not be used
        SPDLOG_WARN("CDC functionality disabled in simplified processor");
        return "{}";
    }

    std::map<uint32_t, std::string> MessageFactory::create_multi_depth_json(
        const InternalOrderBookSnapshot &snapshot,
        const std::vector<uint32_t> &depth_levels) const {
        std::map<uint32_t, std::string> result;

        for (uint32_t depth: depth_levels) {
            // Only create snapshot if we have enough levels
            if (snapshot.bid_levels.size() >= depth && snapshot.ask_levels.size() >= depth) {
                result[depth] = create_snapshot_json(snapshot, depth);
            } else {
                SPDLOG_DEBUG("Insufficient depth for symbol {}: requested={}, available_bids={}, available_asks={}",
                             snapshot.symbol, depth, snapshot.bid_levels.size(), snapshot.ask_levels.size());
            }
        }

        return result;
    }

    std::string MessageFactory::format_price(uint64_t price_scaled) const {
        // Assuming price is scaled by 10000 (4 decimal places)
        double price = static_cast<double>(price_scaled) / std::pow(10, config_.price_decimals);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(config_.price_decimals) << price;
        return ss.str();
    }

    std::string MessageFactory::format_quantity(uint64_t quantity_scaled) const {
        // Assuming quantity is scaled by 100 (2 decimal places)
        double quantity = static_cast<double>(quantity_scaled) / std::pow(10, config_.quantity_decimals);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(config_.quantity_decimals) << quantity;
        return ss.str();
    }

    nlohmann::json MessageFactory::price_level_to_json(const PriceLevel &level, OrderSide side,
                                                       const std::string &symbol) const {
        nlohmann::json j;

        j["symbol"] = symbol;
        j["side"] = side_to_string(side);
        j["price"] = format_price(level.price);
        j["quantity"] = format_quantity(level.quantity);
        j["number_of_orders"] = level.num_orders;

        // Add exchanges array
        if (!level.exchanges.empty()) {
            j["exchanges"] = level.exchanges;
        } else {
            // Default to configured exchange
            j["exchanges"] = nlohmann::json::array({config_.exchange_name});
        }

        return j;
    }

    void MessageFactory::add_common_fields(nlohmann::json &j, const std::string &symbol,
                                           uint64_t sequence, uint64_t timestamp) const {
        j["symbol"] = symbol;

        if (config_.include_sequence) {
            j["sequence"] = sequence;
        }

        if (config_.include_timestamp) {
            j["timestamp"] = timestamp;

            // Also add human-readable timestamp
            auto timestamp_ms = timestamp / 1000;
            auto time_point = std::chrono::system_clock::from_time_t(timestamp_ms / 1000);
            auto ms_part = timestamp_ms % 1000;

            std::time_t tt = std::chrono::system_clock::to_time_t(time_point);
            std::ostringstream ss;
            ss << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%S");
            ss << "." << std::setfill('0') << std::setw(3) << ms_part << "Z";
            j["timestamp_iso"] = ss.str();
        }
    }

    std::string MessageFactory::side_to_string(OrderSide side) {
        switch (side) {
            case OrderSide::Buy: return "bid";
            case OrderSide::Sell: return "ask";
            default: return "unknown";
        }
    }

    std::string MessageFactory::cdc_event_type_to_string(CDCEventType type) {
        // CDC functionality disabled
        return "disabled";
    }

    // KafkaMessage implementation
    KafkaMessage::KafkaMessage(const std::string &t, const std::string &k, const std::string &p, uint32_t part)
        : topic(t), key(k), payload(p), partition(part) {
    }

    // MessageRouter::TopicConfig implementation
    MessageRouter::TopicConfig::TopicConfig()
        : snapshot_topic_prefix("market_depth.")  // Changed to new format
          , cdc_topic("market_depth_cdc")  // Keep for compatibility but not used
          , use_depth_in_topic(false)  // Disabled - we use symbol in topic now
          , use_symbol_partitioning(true)
          , num_partitions(8) {  // Default to 8 partitions as requested
    }

    // MessageRouter implementation
    MessageRouter::MessageRouter(const TopicConfig &config) : config_(config) {
        SPDLOG_DEBUG("MessageRouter created with snapshot_prefix={}, partitions={}",
                     config_.snapshot_topic_prefix, config_.num_partitions);
    }

    MessageRouter::MessageRouter() : config_() {
    }

    KafkaMessage MessageRouter::route_snapshot(const std::string &symbol, uint32_t depth,
                                               const std::string &json_payload) const {
        // New format: market_depth.[SYMBOL_NAME]
        std::string topic = config_.snapshot_topic_prefix + symbol;

        uint32_t partition = config_.use_symbol_partitioning ? calculate_partition(symbol) : 0;

        SPDLOG_TRACE("Routing snapshot for symbol {} to topic {} partition {}", symbol, topic, partition);

        return KafkaMessage(topic, symbol, json_payload, partition);
    }

    KafkaMessage MessageRouter::route_cdc(const std::string &symbol,
                                          const std::string &json_payload) const {
        // CDC routing disabled but kept for interface compatibility
        SPDLOG_WARN("CDC routing disabled in simplified processor");
        uint32_t partition = config_.use_symbol_partitioning ? calculate_partition(symbol) : 0;
        return KafkaMessage(config_.cdc_topic, symbol, json_payload, partition);
    }

    uint32_t MessageRouter::calculate_partition(const std::string &symbol) const {
        return hasher_(symbol) % config_.num_partitions;
    }

} // namespace market_depth