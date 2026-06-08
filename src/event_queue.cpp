#include "event_queue.hpp"

EventQueue::EventQueue(std::size_t max_size)
    : max_size_(max_size) {}

bool EventQueue::push(QueuedEvent event) {
    if (max_size_ != 0 && queue_.size() >= max_size_) {
        ++dropped_;
        return false;
    }
    if (event.sequence == 0) {
        event.sequence = next_sequence_++;
    }
    queue_.push(event);
    return true;
}

bool EventQueue::empty() const {
    return queue_.empty();
}

std::size_t EventQueue::size() const {
    return queue_.size();
}

bool EventQueue::has_due(std::uint64_t timestamp) const {
    return !queue_.empty() && queue_.top().timestamp <= timestamp;
}

QueuedEvent EventQueue::pop_next() {
    QueuedEvent event = queue_.top();
    queue_.pop();
    return event;
}

std::vector<QueuedEvent> EventQueue::pop_due(std::uint64_t timestamp) {
    std::vector<QueuedEvent> events;
    while (has_due(timestamp)) {
        events.push_back(pop_next());
    }
    return events;
}

void EventQueue::clear() {
    while (!queue_.empty()) {
        queue_.pop();
    }
    next_sequence_ = 1;
    dropped_ = 0;
}

std::size_t EventQueue::dropped() const {
    return dropped_;
}

std::size_t EventQueue::capacity() const {
    return max_size_;
}
