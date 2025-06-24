/**
 * @file    OrderBook.hpp
 * @brief   Order book state management and CDC generation
 *
 * Developer: Equix Technologies
 * Copyright: Equix Technologies Pty Ltd
 * Created: June 2025
 *
 * Description:
 *   Manages order book state for individual symbols, handles snapshot updates,
 *   and generates CDC events for downstream consumers. Optimized for high-frequency
 *   market data processing with minimal latency and memory allocation.
 */

#pragma once

#ifndef ORDER_BOOK_HPP_
#define ORDER_BOOK_HPP_

#include "OrderBookTypes.hpp"
#include "orderbook_generated.h"
#include <chrono>
#include <functional>
#include <memory>
#include <shared_mutex>

namespace market_depth {

// Forward declare FlatBuffers types from generated code
namespace fb = ::md;

/**
 * @brief Callback function type for CDC events
 */
using CDCCallback = std::function<void(const CDCEvent&)>;

/**
 * @brief Order book manager for a single symbol
 */
class OrderBook {
public:
    explicit OrderBook(const std::string& symbol,
                      const DepthConfig& config = DepthConfig(),
                      CDCCallback cdc_callback = nullptr);

    bool process_snapshot(const fb::OrderBookSnapshot* snapshot);

    const InternalOrderBookSnapshot& get_snapshot() const { return current_snapshot_; }
    const std::string& get_symbol() const { return symbol_; }
    uint64_t get_message_count() const { return message_count_; }
    uint64_t get_last_sequence() const { return current_snapshot_.sequence; }
    bool is_initialized() const { return initialized_; }

    void set_cdc_callback(CDCCallback callback) { cdc_callback_ = callback; }

private:
    void process_price_levels(const ::flatbuffers::Vector<::flatbuffers::Offset<fb::OrderMsgLevel>>* levels,
                             OrderSide side, uint64_t sequence);

    void generate_cdc_events(const std::map<uint64_t, PriceLevel, std::greater<uint64_t>>& old_levels,
                           const std::map<uint64_t, PriceLevel, std::greater<uint64_t>>& new_levels,
                           OrderSide side, uint64_t sequence);

    void generate_cdc_events(const std::map<uint64_t, PriceLevel>& old_levels,
                           const std::map<uint64_t, PriceLevel>& new_levels,
                           OrderSide side, uint64_t sequence);

    void emit_cdc_event(CDCEventType type, OrderSide side, const PriceLevel& level, uint64_t sequence);

    static uint64_t get_timestamp();

private:
    std::string symbol_;
    DepthConfig config_;
    CDCCallback cdc_callback_;

    InternalOrderBookSnapshot current_snapshot_;
    InternalOrderBookSnapshot previous_snapshot_;

    uint64_t message_count_;
    bool initialized_;
};

/**
 * @brief Order book manager for multiple symbols
 */
class OrderBookManager {
public:
    explicit OrderBookManager(const DepthConfig& config = DepthConfig(),
                            CDCCallback global_cdc_callback = nullptr);

    OrderBook* get_or_create_orderbook(const std::string& symbol);
    bool process_snapshot(const fb::OrderBookSnapshot* snapshot);
    std::vector<std::string> get_tracked_symbols() const;
    ProcessingStats get_aggregate_stats() const;
    void set_global_cdc_callback(CDCCallback callback) { global_cdc_callback_ = callback; }

private:
    DepthConfig config_;
    CDCCallback global_cdc_callback_;

    std::unordered_map<std::string, std::unique_ptr<OrderBook>> orderbooks_;
    mutable std::shared_mutex orderbooks_mutex_;

    ProcessingStats stats_;
    mutable std::mutex stats_mutex_;
};

} // namespace market_depth

#endif /* ORDER_BOOK_HPP_ */