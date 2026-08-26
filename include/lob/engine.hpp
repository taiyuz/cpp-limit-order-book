#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "lob/order_book.hpp"
#include "lob/pool.hpp"
#include "lob/types.hpp"

namespace lob {

using Result = std::expected<FillReport, Error>;
using CancelResult = std::expected<void, Error>;

// Single-instrument matching engine. Not thread-safe: one matching thread owns
// the book. See DESIGN.md for a lock-free / multi-instrument sketch.
class MatchingEngine {
public:
    explicit MatchingEngine(std::size_t expected_orders = 1 << 16, std::size_t pool_block = 4096);

    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;
    MatchingEngine(MatchingEngine&&) = delete;
    MatchingEngine& operator=(MatchingEngine&&) = delete;
    ~MatchingEngine() = default;

    // Limit: match, then rest any remainder (GTC). Market: match, cancel leftover.
    // Trade records from this call are in last_trades() until the next mutating call.
    [[nodiscard]] Result submit(Side side, OrderType type, Price price, Qty qty);

    [[nodiscard]] CancelResult cancel(OrderId id);

    // Same-price qty decrease keeps time priority. Qty increase or price change
    // loses time priority (order goes to the back of its (new) level). A price
    // change that crosses the spread is aggressive and matches immediately.
    [[nodiscard]] Result modify(OrderId id, Price new_price, Qty new_qty);

    [[nodiscard]] std::span<const Trade> last_trades() const noexcept { return trades_; }

    [[nodiscard]] std::optional<Price> best_bid() const noexcept { return book_.best_bid(); }
    [[nodiscard]] std::optional<Price> best_ask() const noexcept { return book_.best_ask(); }
    [[nodiscard]] TopOfBook top() const noexcept;
    [[nodiscard]] Qty quantity_at(Side side, Price px) const noexcept {
        return book_.quantity_at(side, px);
    }
    [[nodiscard]] std::size_t resting_orders() const noexcept { return index_.size(); }
    [[nodiscard]] std::size_t bid_levels() const noexcept { return book_.bid_level_count(); }
    [[nodiscard]] std::size_t ask_levels() const noexcept { return book_.ask_level_count(); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }

private:
    Qty match_buy(OrderNode* taker, bool market);
    Qty match_sell(OrderNode* taker, bool market);
    void rest(OrderNode* node);
    void destroy_resting(OrderNode* node);
    Result finish(OrderNode* node, Qty filled, bool market);

    NodePool pool_;
    OrderBook book_;
    std::unordered_map<OrderId, OrderNode*> index_;
    std::vector<Trade> trades_;
    OrderId next_id_{1};
    std::uint64_t next_seq_{1};
};

}  // namespace lob
