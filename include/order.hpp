#pragma once

#include <cstdint>

enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    Limit,
    Market
};

// Compact value type for the hot matching path. Incoming orders usually live on
// the stack for one call; resting limit orders are moved into book containers.
struct Order {
    std::uint64_t id;
    Side side;
    OrderType type;
    int price;
    int quantity;
    std::uint64_t timestamp;
};
