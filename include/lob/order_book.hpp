#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>

#include "lob/price_level.hpp"

namespace lob {

// Dual-sided book. Price levels live in node-based maps so PriceLevel* stored on
// OrderNode stay valid across inserts of other prices. BBO is begin() of each
// map, which is O(1). Distinct prices P is typically tens to low thousands, so
// O(log P) insert/erase of a level is cheaper than a vector rebuild and simpler
// than a densified tick ladder unless the instrument is extremely tight.
class OrderBook {
public:
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskMap = std::map<Price, PriceLevel, std::less<Price>>;

    void insert(OrderNode* n) {
        if (n->side == Side::Buy) {
            auto [it, _] = bids_.try_emplace(n->price, n->price);
            it->second.push_back(n);
        } else {
            auto [it, _] = asks_.try_emplace(n->price, n->price);
            it->second.push_back(n);
        }
    }

    // Unlink the node. If its level is now empty, drop the map entry.
    void remove(OrderNode* n) {
        PriceLevel* level = n->level;
        const Side side = n->side;
        const Price px = n->price;
        level->unlink(n);
        if (level->empty()) {
            erase_level(side, px);
        }
    }

    void erase_level(Side side, Price px) {
        if (side == Side::Buy) {
            bids_.erase(px);
        } else {
            asks_.erase(px);
        }
    }

    [[nodiscard]] PriceLevel* best_bid_level() noexcept {
        return bids_.empty() ? nullptr : &bids_.begin()->second;
    }
    [[nodiscard]] PriceLevel* best_ask_level() noexcept {
        return asks_.empty() ? nullptr : &asks_.begin()->second;
    }
    [[nodiscard]] const PriceLevel* best_bid_level() const noexcept {
        return bids_.empty() ? nullptr : &bids_.begin()->second;
    }
    [[nodiscard]] const PriceLevel* best_ask_level() const noexcept {
        return asks_.empty() ? nullptr : &asks_.begin()->second;
    }

    [[nodiscard]] std::optional<Price> best_bid() const noexcept {
        if (bids_.empty()) {
            return std::nullopt;
        }
        return bids_.begin()->first;
    }
    [[nodiscard]] std::optional<Price> best_ask() const noexcept {
        if (asks_.empty()) {
            return std::nullopt;
        }
        return asks_.begin()->first;
    }

    [[nodiscard]] Qty quantity_at(Side side, Price px) const noexcept {
        if (side == Side::Buy) {
            auto it = bids_.find(px);
            return it == bids_.end() ? Qty{0} : it->second.total_qty();
        }
        auto it = asks_.find(px);
        return it == asks_.end() ? Qty{0} : it->second.total_qty();
    }

    [[nodiscard]] std::size_t bid_level_count() const noexcept { return bids_.size(); }
    [[nodiscard]] std::size_t ask_level_count() const noexcept { return asks_.size(); }
    [[nodiscard]] bool empty() const noexcept { return bids_.empty() && asks_.empty(); }

    [[nodiscard]] const BidMap& bids() const noexcept { return bids_; }
    [[nodiscard]] const AskMap& asks() const noexcept { return asks_; }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        // rb-tree node overhead is implementation-defined; 3 pointers + color is
        // the usual lower bound. This is an estimate for the harness, not RSS.
        constexpr std::size_t kTreeNode = 4 * sizeof(void*) + sizeof(Price) + sizeof(PriceLevel);
        return bids_.size() * kTreeNode + asks_.size() * kTreeNode;
    }

private:
    BidMap bids_{};
    AskMap asks_{};
};

}  // namespace lob
