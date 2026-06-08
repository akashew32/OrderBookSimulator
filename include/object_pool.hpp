#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

template <typename T>
class ObjectPool {
public:
    // Move-only RAII token. Destroying the handle returns the object to the
    // pool, which keeps ownership explicit without exposing delete to callers.
    class Handle {
    public:
        Handle() = default;
        Handle(T* object, ObjectPool* owner)
            : object_(object), owner_(owner) {}

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&& other) noexcept
            : object_(other.object_), owner_(other.owner_) {
            other.object_ = nullptr;
            other.owner_ = nullptr;
        }

        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                release();
                object_ = other.object_;
                owner_ = other.owner_;
                other.object_ = nullptr;
                other.owner_ = nullptr;
            }
            return *this;
        }

        ~Handle() {
            release();
        }

        T& get() { return *object_; }
        const T& get() const { return *object_; }
        T* operator->() { return object_; }
        const T* operator->() const { return object_; }
        explicit operator bool() const { return object_ != nullptr; }

    private:
        void release() {
            if (object_ && owner_) {
                owner_->release(object_);
                object_ = nullptr;
                owner_ = nullptr;
            }
        }

        T* object_ = nullptr;
        ObjectPool* owner_ = nullptr;
    };

    explicit ObjectPool(std::size_t preallocate = 0) {
        storage_.reserve(preallocate);
        free_list_.reserve(preallocate);
        for (std::size_t i = 0; i < preallocate; ++i) {
            storage_.push_back(std::make_unique<T>());
            free_list_.push_back(storage_.back().get());
        }
    }

    template <typename... Args>
    Handle acquire(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        T* object = nullptr;
        if (free_list_.empty()) {
            storage_.push_back(std::make_unique<T>());
            object = storage_.back().get();
            ++allocated_after_start_;
        } else {
            object = free_list_.back();
            free_list_.pop_back();
            ++reused_;
        }
        *object = T{std::forward<Args>(args)...};
        return Handle(object, this);
    }

    std::size_t capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.size();
    }

    std::size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return free_list_.size();
    }

    std::size_t allocated_after_start() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return allocated_after_start_;
    }

    std::size_t reused() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return reused_;
    }

private:
    void release(T* object) {
        std::lock_guard<std::mutex> lock(mutex_);
        free_list_.push_back(object);
    }

    mutable std::mutex mutex_;
    // storage_ owns the heap objects; free_list_ only borrows pointers to
    // currently available slots.
    std::vector<std::unique_ptr<T>> storage_;
    std::vector<T*> free_list_;
    std::size_t allocated_after_start_ = 0;
    std::size_t reused_ = 0;
};
