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

// Time-in-force is a matching rule, not a resting-node field: only GTC can rest.
enum class TimeInForce : std::uint8_t {
    Gtc = 0,  // match, then rest any remainder (limit only; market never rests)
    Ioc = 1,  // match now, cancel leftover; never rests
    Fok = 2,  // fill the entire qty now or reject; never partials, never rests
};

enum class Error : std::uint8_t {
    InvalidQty = 1,
    InvalidPrice = 2,
    NotFound = 3,
    WouldNotFill = 4,  // FOK could not take its full qty against crossing liquidity
};

inline constexpr std::string_view to_string(Side s) noexcept {
    return s == Side::Buy ? "buy" : "sell";
}

inline constexpr std::string_view to_string(OrderType t) noexcept {
    return t == OrderType::Limit ? "limit" : "market";
}

inline constexpr std::string_view to_string(TimeInForce t) noexcept {
    switch (t) {
        case TimeInForce::Gtc:
            return "gtc";
        case TimeInForce::Ioc:
            return "ioc";
        case TimeInForce::Fok:
            return "fok";
    }
    return "unknown";
}

inline constexpr std::string_view to_string(Error e) noexcept {
    switch (e) {
        case Error::InvalidQty:
            return "invalid_qty";
        case Error::InvalidPrice:
            return "invalid_price";
        case Error::NotFound:
            return "not_found";
        case Error::WouldNotFill:
            return "would_not_fill";
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
