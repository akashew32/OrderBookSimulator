#pragma once

#include <cstdint>
#include <queue>
#include <vector>

#include "order.hpp"

enum class EventType {
    OrderSubmission,
    CancelOrder,
    ModifyOrder,
    BookUpdate,
    ReplayEvent
};

struct QueuedEvent {
    std::uint64_t timestamp = 0;
    std::uint64_t sequence = 0;
    EventType type = EventType::OrderSubmission;
    Order order{};
    std::uint64_t order_id = 0;
    int new_price = 0;
    int new_quantity = 0;
    std::uint64_t source_timestamp = 0;
    int intended_price = 0;
    bool marketable_at_submission = false;
    bool strategy_event = false;
};

class EventQueue {
public:
    explicit EventQueue(std::size_t max_size = 4096);
    bool push(QueuedEvent event);
    bool empty() const;
    std::size_t size() const;
    bool has_due(std::uint64_t timestamp) const;
    QueuedEvent pop_next();
    std::vector<QueuedEvent> pop_due(std::uint64_t timestamp);
    void clear();
    std::size_t dropped() const;
    std::size_t capacity() const;

private:
    struct Later {
        bool operator()(const QueuedEvent& a, const QueuedEvent& b) const {
            if (a.timestamp == b.timestamp) {
                return a.sequence > b.sequence;
            }
            return a.timestamp > b.timestamp;
        }
    };

    std::priority_queue<QueuedEvent, std::vector<QueuedEvent>, Later> queue_;
    std::uint64_t next_sequence_ = 1;
    std::size_t max_size_ = 0;
    std::size_t dropped_ = 0;
};
