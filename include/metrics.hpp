#pragma once

#include <cstdint>

class Metrics {
public:
    void record_order();
    void record_trade(int quantity);
    void record_cancel();
    void record_modify();
    void record_latency(double latency_us);
    void record_fill_delay(double delay_ms);
    void record_missed_opportunity();
    void record_slippage(double slippage);

    std::uint64_t orders_processed() const;
    std::uint64_t trades_executed() const;
    std::uint64_t volume_executed() const;
    std::uint64_t canceled_orders() const;
    std::uint64_t modified_orders() const;
    std::uint64_t missed_trade_opportunities() const;
    double average_latency_us() const;
    double average_fill_delay_ms() const;
    double total_slippage() const;

private:
    std::uint64_t orders_processed_ = 0;
    std::uint64_t trades_executed_ = 0;
    std::uint64_t volume_executed_ = 0;
    std::uint64_t canceled_orders_ = 0;
    std::uint64_t modified_orders_ = 0;
    std::uint64_t missed_trade_opportunities_ = 0;
    double total_latency_us_ = 0.0;
    std::uint64_t latency_samples_ = 0;
    double total_fill_delay_ms_ = 0.0;
    std::uint64_t fill_delay_samples_ = 0;
    double total_slippage_ = 0.0;
};
