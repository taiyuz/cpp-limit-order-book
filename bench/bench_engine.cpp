#include <benchmark/benchmark.h>

#include <vector>

#include "lob/engine.hpp"

using namespace lob;

static void BM_RestNoCross(benchmark::State& state) {
    MatchingEngine eng(static_cast<std::size_t>(state.max_iterations) + 1024, 4096);
    std::uint64_t i = 0;
    for (auto _ : state) {
        const Side side = (i & 1u) ? Side::Sell : Side::Buy;
        const Price px = side == Side::Buy ? Price{1 + static_cast<Price>(i % 128)}
                                           : Price{10'000 + static_cast<Price>(i % 128)};
        auto r = eng.submit(side, OrderType::Limit, px, 1);
        benchmark::DoNotOptimize(r);
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RestNoCross);

static void BM_Cancel(benchmark::State& state) {
    MatchingEngine eng(1 << 20, 4096);
    std::vector<OrderId> ids;
    ids.reserve(1 << 20);
    for (int k = 0; k < (1 << 16); ++k) {
        auto r = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
        ids.push_back(r->order_id);
    }
    std::size_t k = 0;
    for (auto _ : state) {
        if (k >= ids.size()) {
            state.SkipWithError("ran out of resting orders");
            break;
        }
        auto r = eng.cancel(ids[k++]);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Cancel);

static void BM_Cross(benchmark::State& state) {
    MatchingEngine eng(1 << 20, 4096);
    for (int k = 0; k < (1 << 18); ++k) {
        (void)eng.submit(Side::Sell, OrderType::Limit, 100, 1);
    }
    for (auto _ : state) {
        auto r = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
        benchmark::DoNotOptimize(r);
        if (!r || r->filled != 1) {
            state.SkipWithError("book exhausted");
            break;
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Cross);

BENCHMARK_MAIN();
