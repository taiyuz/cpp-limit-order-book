#include "lob/engine.hpp"

namespace lob {

MatchingEngine::MatchingEngine(std::size_t expected_orders, std::size_t pool_block)
    : pool_(pool_block) {
    index_.reserve(expected_orders);
    trades_.reserve(64);
}

TopOfBook MatchingEngine::top() const noexcept {
    TopOfBook t{};
    if (const PriceLevel* lvl = book_.best_bid_level()) {
        t.has_bid = true;
        t.bid_price = lvl->price();
        t.bid_qty = lvl->total_qty();
    }
    if (const PriceLevel* lvl = book_.best_ask_level()) {
        t.has_ask = true;
        t.ask_price = lvl->price();
        t.ask_qty = lvl->total_qty();
    }
    return t;
}

std::size_t MatchingEngine::memory_bytes() const noexcept {
    const std::size_t index_bytes = index_.bucket_count() * sizeof(void*) +
                                    index_.size() * (sizeof(OrderId) + sizeof(OrderNode*));
    const std::size_t trades_bytes = trades_.capacity() * sizeof(Trade);
    return pool_.memory_bytes() + book_.memory_bytes() + index_bytes + trades_bytes;
}

Result MatchingEngine::submit(Side side, OrderType type, Price price, Qty qty) {
    trades_.clear();
    if (qty <= 0) {
        return std::unexpected(Error::InvalidQty);
    }
    if (type == OrderType::Limit && price <= 0) {
        return std::unexpected(Error::InvalidPrice);
    }

    OrderNode* node = pool_.construct();
    node->id = next_id_++;
    node->price = price;
    node->remaining = qty;
    node->seq = next_seq_++;
    node->side = side;

    const bool market = type == OrderType::Market;
    const Qty filled = (side == Side::Buy) ? match_buy(node, market) : match_sell(node, market);
    return finish(node, filled, market);
}

CancelResult MatchingEngine::cancel(OrderId id) {
    trades_.clear();
    auto it = index_.find(id);
    if (it == index_.end()) {
        return std::unexpected(Error::NotFound);
    }
    destroy_resting(it->second);
    return {};
}

Result MatchingEngine::modify(OrderId id, Price new_price, Qty new_qty) {
    trades_.clear();
    if (new_qty <= 0) {
        return std::unexpected(Error::InvalidQty);
    }
    if (new_price <= 0) {
        return std::unexpected(Error::InvalidPrice);
    }

    auto it = index_.find(id);
    if (it == index_.end()) {
        return std::unexpected(Error::NotFound);
    }

    OrderNode* node = it->second;
    const bool price_change = new_price != node->price;
    const bool qty_up = new_qty > node->remaining;

    if (!price_change && new_qty == node->remaining) {
        return FillReport{node->id, 0, node->remaining, true};
    }

    // Qty down, same price: keep place in the FIFO (time priority preserved).
    if (!price_change && !qty_up) {
        const Qty delta = node->remaining - new_qty;
        node->remaining = new_qty;
        node->level->add_qty(-delta);
        return FillReport{node->id, 0, node->remaining, true};
    }

    // Qty up or price change: lose time priority. Pull, maybe re-match, rest.
    const Side side = node->side;
    book_.remove(node);
    index_.erase(it);

    node->price = new_price;
    node->remaining = new_qty;
    node->seq = next_seq_++;

    const Qty filled = (side == Side::Buy) ? match_buy(node, false) : match_sell(node, false);
    return finish(node, filled, /*market=*/false);
}

Qty MatchingEngine::match_buy(OrderNode* taker, bool market) {
    Qty filled = 0;
    while (taker->remaining > 0) {
        PriceLevel* level = book_.best_ask_level();
        if (level == nullptr) {
            break;
        }
        if (!market && level->price() > taker->price) {
            break;
        }

        OrderNode* maker = level->front();
        const Qty fill = taker->remaining < maker->remaining ? taker->remaining : maker->remaining;

        trades_.push_back(Trade{
            .maker_id = maker->id,
            .taker_id = taker->id,
            .price = maker->price,
            .qty = fill,
        });

        maker->remaining -= fill;
        taker->remaining -= fill;
        level->add_qty(-fill);
        filled += fill;

        if (maker->remaining == 0) {
            const Price px = level->price();
            level->unlink(maker);
            index_.erase(maker->id);
            pool_.destroy(maker);
            if (level->empty()) {
                book_.erase_level(Side::Sell, px);
            }
        }
    }
    return filled;
}

Qty MatchingEngine::match_sell(OrderNode* taker, bool market) {
    Qty filled = 0;
    while (taker->remaining > 0) {
        PriceLevel* level = book_.best_bid_level();
        if (level == nullptr) {
            break;
        }
        if (!market && level->price() < taker->price) {
            break;
        }

        OrderNode* maker = level->front();
        const Qty fill = taker->remaining < maker->remaining ? taker->remaining : maker->remaining;

        trades_.push_back(Trade{
            .maker_id = maker->id,
            .taker_id = taker->id,
            .price = maker->price,
            .qty = fill,
        });

        maker->remaining -= fill;
        taker->remaining -= fill;
        level->add_qty(-fill);
        filled += fill;

        if (maker->remaining == 0) {
            const Price px = level->price();
            level->unlink(maker);
            index_.erase(maker->id);
            pool_.destroy(maker);
            if (level->empty()) {
                book_.erase_level(Side::Buy, px);
            }
        }
    }
    return filled;
}

void MatchingEngine::rest(OrderNode* node) {
    book_.insert(node);
    index_.emplace(node->id, node);
}

void MatchingEngine::destroy_resting(OrderNode* node) {
    index_.erase(node->id);
    book_.remove(node);
    pool_.destroy(node);
}

Result MatchingEngine::finish(OrderNode* node, Qty filled, bool market) {
    FillReport report{};
    report.order_id = node->id;
    report.filled = filled;
    report.remaining = node->remaining;

    if (!market && node->remaining > 0) {
        rest(node);
        report.resting = true;
        return report;
    }

    // Market leftover is cancelled (not rested). remaining is the cancelled qty.
    pool_.destroy(node);
    report.resting = false;
    return report;
}

}  // namespace lob
