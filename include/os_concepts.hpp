#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace osdemo {

inline bool diagnostics_enabled() {
    const char* value = std::getenv("LOB_OS_DIAGNOSTICS");
    return value != nullptr && std::string(value) != "0";
}

inline std::mutex& log_mutex() {
    static std::mutex mutex;
    return mutex;
}

inline void log(const std::string& phase, const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex());
    std::cout << "[ostep][" << phase << "] " << message << '\n';
}

inline void log_thread(const std::string& name, const std::string& state) {
    std::ostringstream out;
    out << name << " " << state << " thread_id=" << std::this_thread::get_id();
    log("thread", out.str());
}

class ThreadScope {
public:
    explicit ThreadScope(std::string name)
        : name_(std::move(name)) {
        log_thread(name_, "started");
    }

    ~ThreadScope() {
        log_thread(name_, "stopped");
    }

private:
    std::string name_;
};

class DiagnosticScope {
public:
    DiagnosticScope(std::string name, const void* address, bool heap_owned)
        : name_(std::move(name)), address_(address), heap_owned_(heap_owned) {
        if (diagnostics_enabled()) {
            log("memory", name_ + " created at " + pointer_text() + storage_text());
        }
    }

    ~DiagnosticScope() {
        if (diagnostics_enabled()) {
            log("memory", name_ + " destroyed at " + pointer_text() + storage_text());
        }
    }

private:
    std::string pointer_text() const {
        std::ostringstream out;
        out << address_;
        return out.str();
    }

    std::string storage_text() const {
        return heap_owned_ ? " container/heap-managed" : " stack/local";
    }

    std::string name_;
    const void* address_;
    bool heap_owned_ = false;
};

class Lifecycle {
public:
    void startup() { log("lifecycle", "startup: process entered main()"); }
    void initializing() { log("lifecycle", "initialization: constructing engine, queues, and sockets"); }
    void running() { log("lifecycle", "running: simulation and server loops are active"); }
    void shutdown() { log("lifecycle", "graceful shutdown: stop requested"); }
    void cleanup() { log("lifecycle", "cleanup: joining threads and closing descriptors"); }

    void report(std::uint64_t orders, std::uint64_t trades, std::uint64_t dropped_events) {
        std::ostringstream out;
        out << "final report: orders=" << orders
            << " trades=" << trades
            << " dropped_events=" << dropped_events;
        log("lifecycle", out.str());
    }
};

} // namespace osdemo
