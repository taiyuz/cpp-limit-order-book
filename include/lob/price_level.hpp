#pragma once

#include <cstdint>

#include "lob/types.hpp"

namespace lob {

class PriceLevel;

// 64-byte order node: one cache line. Intrusive FIFO links live on the node so
// cancel/unlink is pointer surgery, not a search through a std::deque.
struct alignas(64) OrderNode {
    OrderNode* prev{nullptr};
    OrderNode* next{nullptr};
    PriceLevel* level{nullptr};
    OrderId id{0};
    Price price{0};
    Qty remaining{0};
    std::uint64_t seq{0};
    Side side{Side::Buy};
};

static_assert(sizeof(OrderNode) == 64, "OrderNode must occupy exactly one cache line");
static_assert(alignof(OrderNode) == 64);

// One price, one FIFO. Head is the oldest resting order (time priority).
class PriceLevel {
public:
    explicit PriceLevel(Price price) noexcept : price_(price) {}

    [[nodiscard]] Price price() const noexcept { return price_; }
    [[nodiscard]] Qty total_qty() const noexcept { return total_qty_; }
    [[nodiscard]] std::uint32_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return head_ == nullptr; }
    [[nodiscard]] OrderNode* front() const noexcept { return head_; }
    [[nodiscard]] OrderNode* back() const noexcept { return tail_; }

    void push_back(OrderNode* n) noexcept {
        n->prev = tail_;
        n->next = nullptr;
        n->level = this;
        if (tail_ != nullptr) {
            tail_->next = n;
        } else {
            head_ = n;
        }
        tail_ = n;
        total_qty_ += n->remaining;
        ++count_;
    }

    // Unlink n. Subtracts n->remaining from the level aggregate, so callers that
    // have already reduced remaining (full fill) must have decremented total_qty
    // by the fill amount first; remaining is then 0 and this is a no-op add.
    void unlink(OrderNode* n) noexcept {
        if (n->prev != nullptr) {
            n->prev->next = n->next;
        } else {
            head_ = n->next;
        }
        if (n->next != nullptr) {
            n->next->prev = n->prev;
        } else {
            tail_ = n->prev;
        }
        total_qty_ -= n->remaining;
        --count_;
        n->prev = nullptr;
        n->next = nullptr;
        n->level = nullptr;
    }

    OrderNode* pop_front() noexcept {
        OrderNode* n = head_;
        if (n != nullptr) {
            unlink(n);
        }
        return n;
    }

    void add_qty(Qty delta) noexcept { total_qty_ += delta; }

private:
    Price price_{0};
    OrderNode* head_{nullptr};
    OrderNode* tail_{nullptr};
    Qty total_qty_{0};
    std::uint32_t count_{0};
};

}  // namespace lob
