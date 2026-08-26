#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "lob/engine.hpp"

using namespace lob;
using clock_type = std::chrono::steady_clock;

struct Stats {
    std::string name;
    std::size_t n{};
    double ops_per_sec{};
    double median_ns{};
    double p95_ns{};
    double p99_ns{};
    std::size_t memory_bytes{};
    std::size_t resting{};
};

static double percentile(std::vector<double>& v, double p) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const double idx = p * static_cast<double>(v.size() - 1);
    const auto i = static_cast<std::size_t>(idx);
    const double frac = idx - static_cast<double>(i);
    if (i + 1 >= v.size()) {
        return v.back();
    }
    return v[i] * (1.0 - frac) + v[i + 1] * frac;
}

template <typename Fn>
static Stats run_workload(const char* name, std::size_t n, Fn&& fn, const MatchingEngine& eng) {
    std::vector<double> samples;
    samples.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto t0 = clock_type::now();
        fn(i);
        const auto t1 = clock_type::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    double sum = 0.0;
    for (double s : samples) {
        sum += s;
    }
    Stats st;
    st.name = name;
    st.n = n;
    st.ops_per_sec = sum > 0.0 ? (static_cast<double>(n) * 1e9 / sum) : 0.0;
    st.median_ns = percentile(samples, 0.50);
    st.p95_ns = percentile(samples, 0.95);
    st.p99_ns = percentile(samples, 0.99);
    st.memory_bytes = eng.memory_bytes();
    st.resting = eng.resting_orders();
    return st;
}

static void print_table(const std::vector<Stats>& rows) {
    std::cout << std::left << std::setw(22) << "workload" << std::right << std::setw(10) << "n"
              << std::setw(14) << "ops/sec" << std::setw(12) << "median ns" << std::setw(12)
              << "p95 ns" << std::setw(12) << "p99 ns" << std::setw(12) << "mem KB" << std::setw(10)
              << "resting" << '\n';
    std::cout << std::string(104, '-') << '\n';
    std::cout << std::fixed;
    for (const auto& r : rows) {
        std::cout << std::left << std::setw(22) << r.name << std::right << std::setw(10) << r.n
                  << std::setw(14) << std::setprecision(0) << r.ops_per_sec << std::setw(12)
                  << std::setprecision(1) << r.median_ns << std::setw(12) << r.p95_ns
                  << std::setw(12) << r.p99_ns << std::setw(12) << std::setprecision(1)
                  << (static_cast<double>(r.memory_bytes) / 1024.0) << std::setw(10) << r.resting
                  << '\n';
    }
}

int main(int argc, char** argv) {
    std::size_t n = 200000;
    if (argc > 1) {
        n = static_cast<std::size_t>(std::stoull(argv[1]));
    }

    std::cout << "lob harness  n=" << n << "  (steady_clock per-op)\n\n";

    std::vector<Stats> rows;

    {
        MatchingEngine eng(n + 16, 4096);
        for (std::size_t i = 0; i < 4096; ++i) {
            (void)eng.submit(Side::Buy, OrderType::Limit, 100, 1);
        }
        for (std::size_t i = 0; i < 4096; ++i) {
            (void)eng.cancel(static_cast<OrderId>(i + 1));
        }
        MatchingEngine cold(n + 16, 4096);
        auto st = run_workload(
            "rest_no_cross",
            n,
            [&](std::size_t i) {
                const Price px = 1 + static_cast<Price>(i % 512);
                const Side side = (i & 1u) ? Side::Sell : Side::Buy;
                const Price book_px = side == Side::Buy ? px : px + 10'000;
                (void)cold.submit(side, OrderType::Limit, book_px, 1);
            },
            cold);
        rows.push_back(st);
    }

    {
        MatchingEngine eng(n + 16, 4096);
        std::vector<OrderId> ids;
        ids.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            auto r = eng.submit(Side::Buy, OrderType::Limit, 1 + static_cast<Price>(i % 64), 1);
            ids.push_back(r->order_id);
        }
        std::size_t k = 0;
        auto st = run_workload("cancel", n, [&](std::size_t) { (void)eng.cancel(ids[k++]); }, eng);
        rows.push_back(st);
    }

    {
        MatchingEngine eng(n + 16, 4096);
        const std::size_t depth = n;
        for (std::size_t i = 0; i < depth; ++i) {
            (void)eng.submit(Side::Sell, OrderType::Limit, 100, 1);
        }
        auto st = run_workload(
            "cross_full_fill",
            n,
            [&](std::size_t) { (void)eng.submit(Side::Buy, OrderType::Limit, 100, 1); },
            eng);
        rows.push_back(st);
    }

    {
        MatchingEngine eng(n + 16, 4096);
        std::vector<OrderId> live;
        live.reserve(n);
        auto st = run_workload(
            "mixed_70_20_10",
            n,
            [&](std::size_t i) {
                const int lane = static_cast<int>(i % 10);
                if (lane < 7) {
                    const Side side = (i & 1u) ? Side::Sell : Side::Buy;
                    const Price px = side == Side::Buy ? Price{90 + static_cast<Price>(i % 10)}
                                                       : Price{110 + static_cast<Price>(i % 10)};
                    auto r = eng.submit(side, OrderType::Limit, px, 1);
                    if (r && r->resting) {
                        live.push_back(r->order_id);
                    }
                } else if (lane < 9) {
                    if (!live.empty()) {
                        (void)eng.cancel(live.back());
                        live.pop_back();
                    }
                } else {
                    const Side side = (i & 1u) ? Side::Buy : Side::Sell;
                    (void)eng.submit(side, OrderType::Market, 0, 1);
                }
            },
            eng);
        rows.push_back(st);
    }

    print_table(rows);
    std::cout << "\nmemory_bytes is an internal estimate (pool slabs + index + tree nodes),\n"
                 "not process RSS. Reproduce: cmake --preset default && cmake --build --preset "
                 "default && ./build/lob_harness [n]\n";
    return 0;
}
