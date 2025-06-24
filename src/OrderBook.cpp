/**
 * @file    OrderBook.cpp
 * @brief   Order book state management implementation
 */

#include "OrderBook.hpp"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <shared_mutex>

namespace md {

OrderBook::OrderBook(const std::string& symbol,
                    const DepthConfig& config,
                    CDCCallback cdc_callback)
    : symbol_(symbol)
    , config_(config)
    , cdc_callback_(cdc_callback)
    , message_count_(0)
    , initialized_(false) {

    current_snapshot_.symbol = symbol;
    previous_snapshot_.symbol = symbol;

    SPDLOG_DEBUG("OrderBook created for symbol: {}", symbol_);
}

bool OrderBook::process_snapshot(const ::md::OrderBookSnapshot* snapshot) {
    if (!snapshot) {
        SPDLOG_ERROR("Null snapshot received for symbol: {}", symbol_);
        return false;
    }

    // Validate symbol matches
    if (snapshot->symbol() && snapshot->symbol()->str() != symbol_) {
        SPDLOG_ERROR("Symbol mismatch: expected {}, got {}", symbol_,
                    snapshot->symbol()->str());
        return false;
    }

    // Store previous state for CDC comparison
    if (initialized_ && config_.enable_cdc) {
        previous_snapshot_ = current_snapshot_;
    }

    // Update sequence and timestamp
    current_snapshot_.sequence = snapshot->seq();
    current_snapshot_.timestamp_us = get_timestamp_us();

    // Update trade info
    current_snapshot_.last_trade_price = snapshot->recent_trade_price();
    current_snapshot_.last_trade_quantity = snapshot->recent_trade_qty();

    // Clear current levels
    current_snapshot_.bid_levels.clear();
    current_snapshot_.ask_levels.clear();

    // Process buy side levels
    if (snapshot->buy_side()) {
        process_price_levels(snapshot->buy_side(), OrderSide::Buy, snapshot->seq());
    }

    // Process sell side levels
    if (snapshot->sell_side()) {
        process_price_levels(snapshot->sell_side(), OrderSide::Sell, snapshot->seq());
    }

    ++message_count_;

    // Generate CDC events if enabled and not first snapshot
    if (initialized_ && config_.enable_cdc && cdc_callback_) {
        generate_cdc_events(previous_snapshot_.bid_levels, current_snapshot_.bid_levels,
                          OrderSide::Buy, snapshot->seq());
        generate_cdc_events(previous_snapshot_.ask_levels, current_snapshot_.ask_levels,
                          OrderSide::Sell, snapshot->seq());
    }

    if (!initialized_) {
        initialized_ = true;
        SPDLOG_DEBUG("OrderBook initialized for symbol: {} with {} bids, {} asks",
                    symbol_, current_snapshot_.bid_levels.size(),
                    current_snapshot_.ask_levels.size());
    }

    return true;
}

void OrderBook::process_price_levels(
    const ::flatbuffers::Vector<::flatbuffers::Offset<::md::OrderMsgLevel>>* levels,
    OrderSide side,
    uint64_t sequence) {

    if (!levels) return;

    for (uint32_t i = 0; i < levels->size() && i < config_.max_price_levels; ++i) {
        const auto* level = levels->Get(i);
        if (!level) continue;

        PriceLevel price_level;
        price_level.price = level->price();
        price_level.quantity = 0;
        price_level.num_orders = 0;

        // Aggregate orders at this price level
        if (level->orders()) {
            for (uint32_t j = 0; j < level->orders()->size(); ++j) {
                const auto* order = level->orders()->Get(j);
                if (order) {
                    price_level.quantity += order->qty();
                    price_level.num_orders++;
                }
            }
        }

        // Add to appropriate side
        if (side == OrderSide::Buy) {
            current_snapshot_.bid_levels[price_level.price] = price_level;
        } else {
            current_snapshot_.ask_levels[price_level.price] = price_level;
        }
    }
}

void OrderBook::generate_cdc_events(
    const std::map<uint64_t, PriceLevel, std::greater<uint64_t>>& old_levels,
    const std::map<uint64_t, PriceLevel, std::greater<uint64_t>>& new_levels,
    OrderSide side,
    uint64_t sequence) {

    // Find removed levels
    for (const auto& [price, old_level] : old_levels) {
        if (new_levels.find(price) == new_levels.end()) {
            emit_cdc_event(CDCEventType::LevelRemoved, side, old_level, sequence);
        }
    }

    // Find added and modified levels
    for (const auto& [price, new_level] : new_levels) {
        auto old_it = old_levels.find(price);
        if (old_it == old_levels.end()) {
            // New level added
            emit_cdc_event(CDCEventType::LevelAdded, side, new_level, sequence);
        } else if (old_it->second != new_level) {
            // Level modified
            emit_cdc_event(CDCEventType::LevelModified, side, new_level, sequence);
        }
    }
}

void OrderBook::generate_cdc_events(
    const std::map<uint64_t, PriceLevel>& old_levels,
    const std::map<uint64_t, PriceLevel>& new_levels,
    OrderSide side,
    uint64_t sequence) {

    // Find removed levels
    for (const auto& [price, old_level] : old_levels) {
        if (new_levels.find(price) == new_levels.end()) {
            emit_cdc_event(CDCEventType::LevelRemoved, side, old_level, sequence);
        }
    }

    // Find added and modified levels
    for (const auto& [price, new_level] : new_levels) {
        auto old_it = old_levels.find(price);
        if (old_it == old_levels.end()) {
            // New level added
            emit_cdc_event(CDCEventType::LevelAdded, side, new_level, sequence);
        } else if (old_it->second != new_level) {
            // Level modified
            emit_cdc_event(CDCEventType::LevelModified, side, new_level, sequence);
        }
    }
}

void OrderBook::emit_cdc_event(CDCEventType type, OrderSide side,
                              const PriceLevel& level, uint64_t sequence) {
    if (!cdc_callback_) return;

    CDCEvent event;
    event.symbol = symbol_;
    event.side = side;
    event.event_type = type;
    event.level = level;
    event.sequence = sequence;
    event.timestamp_us = get_timestamp_us();

    cdc_callback_(event);
}

// OrderBookManager Implementation

OrderBookManager::OrderBookManager(const DepthConfig& config,
                                 CDCCallback global_cdc_callback)
    : config_(config)
    , global_cdc_callback_(global_cdc_callback) {

    SPDLOG_INFO("OrderBookManager created with max_levels={}, depth_levels=[{}]",
               config_.max_price_levels,
               [&]() {
                   std::string levels;
                   for (size_t i = 0; i < config_.depth_levels.size(); ++i) {
                       if (i > 0) levels += ",";
                       levels += std::to_string(config_.depth_levels[i]);
                   }
                   return levels;
               }());
}

OrderBook* OrderBookManager::get_or_create_orderbook(const std::string& symbol) {
    // First try with shared lock for read
    {
        std::shared_lock lock(orderbooks_mutex_);
        auto it = orderbooks_.find(symbol);
        if (it != orderbooks_.end()) {
            return it->second.get();
        }
    }

    // Upgrade to unique lock for write
    std::unique_lock lock(orderbooks_mutex_);

    // Double-check pattern
    auto it = orderbooks_.find(symbol);
    if (it != orderbooks_.end()) {
        return it->second.get();
    }

    // Create new order book
    auto orderbook = std::make_unique<OrderBook>(symbol, config_, global_cdc_callback_);
    OrderBook* ptr = orderbook.get();
    orderbooks_[symbol] = std::move(orderbook);

    SPDLOG_DEBUG("Created new orderbook for symbol: {}", symbol);
    return ptr;
}

bool OrderBookManager::process_snapshot(const ::md::OrderBookSnapshot* snapshot) {
    if (!snapshot || !snapshot->symbol()) {
        SPDLOG_ERROR("Invalid snapshot: null or missing symbol");
        return false;
    }

    std::string symbol = snapshot->symbol()->str();
    OrderBook* orderbook = get_or_create_orderbook(symbol);

    if (!orderbook) {
        SPDLOG_ERROR("Failed to get/create orderbook for symbol: {}", symbol);
        return false;
    }

    bool success = orderbook->process_snapshot(snapshot);

    // Update statistics
    {
        std::lock_guard lock(stats_mutex_);
        if (success) {
            stats_.increment_processed(symbol, snapshot->seq());
        } else {
            stats_.increment_errors();
        }
    }

    return success;
}

std::vector<std::string> OrderBookManager::get_tracked_symbols() const {
    std::shared_lock lock(orderbooks_mutex_);
    std::vector<std::string> symbols;
    symbols.reserve(orderbooks_.size());

    for (const auto& [symbol, orderbook] : orderbooks_) {
        symbols.push_back(symbol);
    }

    return symbols;
}

ProcessingStats OrderBookManager::get_aggregate_stats() const {
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

} // namespace md