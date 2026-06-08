#pragma once

#include <cstdint>

// Compact match result. Trades are returned in vectors so each batch is
// contiguous and cheap to scan in metrics, frontend snapshots, and tests.
struct Trade {
    std::uint64_t buy_order_id;
    std::uint64_t sell_order_id;
    int price;
    int quantity;
    std::uint64_t timestamp;
};
