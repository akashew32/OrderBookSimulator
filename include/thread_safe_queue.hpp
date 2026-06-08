#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

template <typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(std::size_t max_size = 0)
        : max_size_(max_size) {}

    // Producer side. A max_size of 0 means unbounded; otherwise producers block
    // when the queue reaches capacity. That is the backpressure point.
    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_cv_.wait(lock, [this]() {
            return closed_ || max_size_ == 0 || queue_.size() < max_size_;
        });
        if (closed_) {
            return false;
        }
        queue_.push(std::move(value));
        lock.unlock();
        not_empty_cv_.notify_one();
        return true;
    }

    // Non-blocking producer path for overload tests and event drops.
    bool try_push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || (max_size_ != 0 && queue_.size() >= max_size_)) {
                ++dropped_;
                return false;
            }
            queue_.push(std::move(value));
        }
        not_empty_cv_.notify_one();
        return true;
    }

    // Consumer side. Returns false after close() and drain.
    bool pop(T& result) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_cv_.wait(lock, [this]() { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }

        result = std::move(queue_.front());
        queue_.pop();
        lock.unlock();
        not_full_cv_.notify_one();
        return true;
    }

    bool try_pop(T& result) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) {
                return false;
            }

            result = std::move(queue_.front());
            queue_.pop();
        }
        not_full_cv_.notify_one();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    std::size_t dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

    std::size_t capacity() const {
        return max_size_;
    }

private:
    // One mutex owns the queue and its closed/dropped bookkeeping. Producers
    // and consumers release it before notify_one(), avoiding a lock handoff
    // bottleneck and keeping deadlock risk low: there is no second queue lock.
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
    std::size_t max_size_ = 0;
    std::size_t dropped_ = 0;
    bool closed_ = false;
};
