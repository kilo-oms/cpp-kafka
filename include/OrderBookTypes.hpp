/**
 * @file    OrderBookTypes.hpp
 * @brief   Core data types and structures for market depth processing
 *
 * Developer: Equix Technologies
 * Copyright: Equix Technologies Pty Ltd
 * Created: June 2025
 *
 * Description:
 *   Defines core data structures for order book management, market depth levels,
 *   and CDC (Change Data Capture) events for high-frequency trading systems.
 */

#pragma once

#ifndef ORDER_BOOK_TYPES_HPP_
#define ORDER_BOOK_TYPES_HPP_

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

namespace md {

/**
 * @brief Order side enumeration
 */
enum class OrderSide : uint8_t {
    Buy = 0,
    Sell = 1
};

/**
 * @brief CDC event types for change tracking
 */
enum class CDCEventType : uint8_t {
    LevelAdded = 0,
    LevelModified = 1,
    LevelRemoved = 2,
    BookCleared = 3
};

/**
 * @brief Price level in the order book
 */
struct PriceLevel {
    uint64_t price;           // Price in scaled integer format
    uint64_t quantity;        // Total quantity at this level
    uint32_t num_orders;      // Number of orders at this level
    std::vector<std::string> exchanges; // Exchanges contributing to this level

    PriceLevel() : price(0), quantity(0), num_orders(0) {}

    PriceLevel(uint64_t p, uint64_t qty, uint32_t orders = 1)
        : price(p), quantity(qty), num_orders(orders) {}

    bool operator==(const PriceLevel& other) const {
        return price == other.price &&
               quantity == other.quantity &&
               num_orders == other.num_orders;
    }

    bool operator!=(const PriceLevel& other) const {
        return !(*this == other);
    }
};

/**
 * @brief CDC event for tracking order book changes
 */
struct CDCEvent {
    std::string symbol;
    OrderSide side;
    CDCEventType event_type;
    PriceLevel level;
    uint64_t sequence;
    uint64_t timestamp_us;

    CDCEvent() : side(OrderSide::Buy), event_type(CDCEventType::LevelAdded),
                 sequence(0), timestamp_us(0) {}
};

/**
 * @brief Market depth configuration
 */
struct DepthConfig {
    std::vector<uint32_t> depth_levels = {5, 10, 25, 50};
    bool enable_cdc = true;
    bool enable_snapshots = true;
    uint32_t max_price_levels = 100; // Maximum levels to track per side
};

/**
 * @brief Order book snapshot for a single symbol
 */
struct OrderBookSnapshot {
    std::string symbol;
    uint64_t sequence;
    uint64_t timestamp_us;

    // Buy side levels (sorted by price descending - highest first)
    std::map<uint64_t, PriceLevel, std::greater<uint64_t>> bid_levels;

    // Sell side levels (sorted by price ascending - lowest first)
    std::map<uint64_t, PriceLevel> ask_levels;

    // Recent trade info
    uint64_t last_trade_price;
    uint64_t last_trade_quantity;

    OrderBookSnapshot() : sequence(0), timestamp_us(0),
                         last_trade_price(0), last_trade_quantity(0) {}

    /**
     * @brief Get top N bid levels
     */
    std::vector<PriceLevel> get_top_bids(uint32_t depth) const {
        std::vector<PriceLevel> result;
        result.reserve(depth);

        auto it = bid_levels.begin();
        for (uint32_t i = 0; i < depth && it != bid_levels.end(); ++i, ++it) {
            result.push_back(it->second);
        }
        return result;
    }

    /**
     * @brief Get top N ask levels
     */
    std::vector<PriceLevel> get_top_asks(uint32_t depth) const {
        std::vector<PriceLevel> result;
        result.reserve(depth);

        auto it = ask_levels.begin();
        for (uint32_t i = 0; i < depth && it != ask_levels.end(); ++i, ++it) {
            result.push_back(it->second);
        }
        return result;
    }

    /**
     * @brief Check if order book has sufficient depth
     */
    bool has_sufficient_depth(uint32_t min_levels = 1) const {
        return bid_levels.size() >= min_levels && ask_levels.size() >= min_levels;
    }
};

/**
 * @brief Statistics for monitoring system performance
 */
struct ProcessingStats {
    uint64_t messages_processed = 0;
    uint64_t cdc_events_generated = 0;
    uint64_t snapshots_published = 0;
    uint64_t processing_errors = 0;
    uint64_t last_sequence_processed = 0;

    // Per-symbol stats
    std::unordered_map<std::string, uint64_t> symbol_message_counts;
    std::unordered_map<std::string, uint64_t> symbol_last_sequence;

    void increment_processed(const std::string& symbol, uint64_t sequence = 0) {
        ++messages_processed;
        ++symbol_message_counts[symbol];
        if (sequence > 0) {
            symbol_last_sequence[symbol] = sequence;
            last_sequence_processed = std::max(last_sequence_processed, sequence);
        }
    }

    void increment_cdc_events() { ++cdc_events_generated; }
    void increment_snapshots() { ++snapshots_published; }
    void increment_errors() { ++processing_errors; }
};

} // namespace md

#endif /* ORDER_BOOK_TYPES_HPP_ */