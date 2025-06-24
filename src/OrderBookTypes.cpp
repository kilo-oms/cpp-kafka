/**
* @file    OrderBookTypes.cpp
 * @brief   Simplified implementation of core data types
 */

#include "OrderBookTypes.hpp"

namespace market_depth {

    // PriceLevel implementations
    PriceLevel::PriceLevel() : price(0), quantity(0), num_orders(0) {}

    PriceLevel::PriceLevel(uint64_t p, uint64_t qty, uint32_t orders)
        : price(p), quantity(qty), num_orders(orders) {}

    bool PriceLevel::operator==(const PriceLevel& other) const {
        return price == other.price &&
               quantity == other.quantity &&
               num_orders == other.num_orders;
    }

    bool PriceLevel::operator!=(const PriceLevel& other) const {
        return !(*this == other);
    }

    // CDCEvent implementations (kept for compatibility)
    CDCEvent::CDCEvent()
        : side(OrderSide::Buy)
        , event_type(CDCEventType::LevelAdded)
        , sequence(0)
        , timestamp(0) {}

    // InternalOrderBookSnapshot implementations
    InternalOrderBookSnapshot::InternalOrderBookSnapshot()
        : sequence(0)
        , timestamp(0)
        , last_trade_price(0)
        , last_trade_quantity(0) {}

    std::vector<PriceLevel> InternalOrderBookSnapshot::get_top_bids(uint32_t depth) const {
        std::vector<PriceLevel> result;
        result.reserve(depth);

        auto it = bid_levels.begin();
        for (uint32_t i = 0; i < depth && it != bid_levels.end(); ++i, ++it) {
            result.push_back(it->second);
        }
        return result;
    }

    std::vector<PriceLevel> InternalOrderBookSnapshot::get_top_asks(uint32_t depth) const {
        std::vector<PriceLevel> result;
        result.reserve(depth);

        auto it = ask_levels.begin();
        for (uint32_t i = 0; i < depth && it != ask_levels.end(); ++i, ++it) {
            result.push_back(it->second);
        }
        return result;
    }

    bool InternalOrderBookSnapshot::has_sufficient_depth(uint32_t min_levels) const {
        return bid_levels.size() >= min_levels && ask_levels.size() >= min_levels;
    }

} // namespace market_depth