/**
 * @file    OrderBookTypes.hpp
 * @brief   Simplified core data types for market depth processing
 *
 * Developer: Equix Technologies
 * Copyright: Equix Technologies Pty Ltd
 * Created: June 2025
 *
 * Description:
 *   Defines simplified core data structures for market depth levels.
 *   CDC functionality is disabled in this version.
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
 * @brief CDC event types (kept for compatibility but not used)
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
 * @brief CDC event (kept for compatibility but not used in simplified version)
 */
struct CDCEvent {
    std::string symbol;
    OrderSide side;
    CDCEventType event_type;
    PriceLevel level;
    uint64_t sequence;
    uint64_t timestamp;

    CDCEvent();
};

/**
 * @brief Simplified internal order book snapshot
 */
struct InternalOrderBookSnapshot {
    std::string symbol;
    uint64_t sequence;
    uint64_t timestamp;

    std::map<uint64_t, PriceLevel, std::greater<uint64_t>> bid_levels;  // Bids: highest to lowest
    std::map<uint64_t, PriceLevel> ask_levels;                          // Asks: lowest to highest

    uint64_t last_trade_price;
    uint64_t last_trade_quantity;

    InternalOrderBookSnapshot();

    std::vector<PriceLevel> get_top_bids(uint32_t depth) const;
    std::vector<PriceLevel> get_top_asks(uint32_t depth) const;
    bool has_sufficient_depth(uint32_t min_levels = 1) const;
};

} // namespace market_depth

#endif /* ORDER_BOOK_TYPES_HPP_ */