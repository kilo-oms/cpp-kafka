# CBOE Orderbook

Ứng dụng tạo sổ lệnh các giao dịch trên CBOE

## Kiến trúc

Order
│
├─ order_id_: uint64_t
├─ order_price_: uint64_t
├─ order_qty_: uint32_t
├─ order_side_: OrderSide
├─ next_: Order*         // → next order at this price (OrderList linked list)
└─ prev_: Order*         // → prev order at this price

OrderList
│
├─ head_ → Order*        // points to first order in doubly-linked list
├─ tail_ → Order*        // points to last
├─ total_qty_: uint32_t  // sum of all order quantities at this price

OrderBook
│
├─ buy_side_:  btree_map<price → OrderList>
├─ sell_side_: btree_map<price → OrderList>
├─ id_to_order_: flat_hash_map<order_id → Order*>
├─ recent_trade_price_, recent_trade_qty_: bookkeeping

OrderBook
│
├─ buy_side_ (btree_map)
│     ├─ [price 98] → OrderList (FIFO): Order* <-> Order* <-> ...
│     ├─ [price 99] → OrderList (...)
│     └─ etc.
│
├─ sell_side_ (btree_map)
│     └─ [price ...] → OrderList (...)
│
└─ id_to_order_ (flat_hash_map)
      ├─ [order_id 1] → Order*
      ├─ [order_id 2] → Order*
      └─ etc.

## Yêu cầu

### Dependencies:
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libboost-all-dev \
    librdkafka-dev \
    nlohmann-json3-dev \
    pkg-config
sudo apt install librdkafka-dev
sudo apt-get install libyaml-cpp-dev
sudo apt-get install libboost-dev
# macOS
brew install boost librdkafka nlohmann-json cmake
brew install librdkafka
```

### Build

```bash
rm -rf build
mkdir build
cd build
# Build dependencies in release mode
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
# Build app
make release
```