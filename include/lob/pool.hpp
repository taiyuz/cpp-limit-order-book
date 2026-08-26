#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "lob/price_level.hpp"

namespace lob {

// Slab allocator with a free-list. Order nodes are never new'd/delete'd one at a
// time on the hot path: construct() is a pointer pop, destroy() is a pointer push.
class NodePool {
public:
    explicit NodePool(std::size_t block_nodes = 4096) : block_nodes_(block_nodes) {
        if (block_nodes_ == 0) {
            block_nodes_ = 4096;
        }
        grow();
    }

    NodePool(const NodePool&) = delete;
    NodePool& operator=(const NodePool&) = delete;
    NodePool(NodePool&&) = delete;
    NodePool& operator=(NodePool&&) = delete;
    ~NodePool() = default;

    [[nodiscard]] OrderNode* construct() {
        if (free_head_ == nullptr) {
            grow();
        }
        OrderNode* n = free_head_;
        free_head_ = n->next;
        *n = OrderNode{};
        ++in_use_;
        return n;
    }

    void destroy(OrderNode* n) noexcept {
        if (n == nullptr) {
            return;
        }
        n->prev = nullptr;
        n->level = nullptr;
        n->id = 0;
        n->next = free_head_;
        free_head_ = n;
        --in_use_;
    }

    [[nodiscard]] std::size_t in_use() const noexcept { return in_use_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t block_nodes() const noexcept { return block_nodes_; }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return capacity_ * sizeof(OrderNode);
    }

private:
    void grow() {
        auto block = std::make_unique<OrderNode[]>(block_nodes_);
        OrderNode* nodes = block.get();
        for (std::size_t i = 0; i + 1 < block_nodes_; ++i) {
            nodes[i].next = &nodes[i + 1];
        }
        nodes[block_nodes_ - 1].next = free_head_;
        free_head_ = nodes;
        blocks_.push_back(std::move(block));
        capacity_ += block_nodes_;
    }

    std::vector<std::unique_ptr<OrderNode[]>> blocks_{};
    OrderNode* free_head_{nullptr};
    std::size_t block_nodes_;
    std::size_t capacity_{0};
    std::size_t in_use_{0};
};

}  // namespace lob
