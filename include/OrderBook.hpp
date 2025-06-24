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

namespace market_depth {

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
    uint64_t price;
    uint64_t quantity;
    uint32_t num_orders;
    std::vector<std::string> exchanges;

    PriceLevel();
    PriceLevel(uint64_t p, uint64_t qty, uint32_t orders = 1);

    bool operator==(const PriceLevel& other) const;
    bool operator!=(const PriceLevel& other) const;
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

    CDCEvent();
};

/**
 * @brief Market depth configuration
 */
struct DepthConfig {
    std::vector<uint32_t> depth_levels;
    bool enable_cdc;
    bool enable_snapshots;
    uint32_t max_price_levels;

    DepthConfig();
};

/**
 * @brief Internal order book snapshot
 */
struct InternalOrderBookSnapshot {
    std::string symbol;
    uint64_t sequence;
    uint64_t timestamp_us;

    std::map<uint64_t, PriceLevel, std::greater<uint64_t>> bid_levels;
    std::map<uint64_t, PriceLevel> ask_levels;

    uint64_t last_trade_price;
    uint64_t last_trade_quantity;

    InternalOrderBookSnapshot();

    std::vector<PriceLevel> get_top_bids(uint32_t depth) const;
    std::vector<PriceLevel> get_top_asks(uint32_t depth) const;
    bool has_sufficient_depth(uint32_t min_levels = 1) const;
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