#pragma once

#include <cstdint>
#include <string_view>

namespace lob {

// Prices are integer ticks. Never float/double on the matching path: tick size is
// an instrument property applied at the edge (gateway), not inside the book.
using Price = std::int64_t;
using Qty = std::int64_t;
using OrderId = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

enum class OrderType : std::uint8_t {
    Limit = 0,
    Market = 1,
};

enum class Error : std::uint8_t {
    InvalidQty = 1,
    InvalidPrice = 2,
    NotFound = 3,
};

inline constexpr std::string_view to_string(Side s) noexcept {
    return s == Side::Buy ? "buy" : "sell";
}

inline constexpr std::string_view to_string(OrderType t) noexcept {
    return t == OrderType::Limit ? "limit" : "market";
}

inline constexpr std::string_view to_string(Error e) noexcept {
    switch (e) {
        case Error::InvalidQty:
            return "invalid_qty";
        case Error::InvalidPrice:
            return "invalid_price";
        case Error::NotFound:
            return "not_found";
    }
    return "unknown";
}

inline constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

struct Trade {
    OrderId maker_id{};
    OrderId taker_id{};
    Price price{};
    Qty qty{};
};

struct FillReport {
    OrderId order_id{};
    Qty filled{};
    Qty remaining{};
    bool resting{};
};

struct TopOfBook {
    Price bid_price{};
    Price ask_price{};
    Qty bid_qty{};
    Qty ask_qty{};
    bool has_bid{};
    bool has_ask{};
};

}  // namespace lob
