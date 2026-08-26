#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "lob/engine.hpp"
#include "lob/types.hpp"
#include "lob/version.hpp"

using namespace lob;

TEST(Version, NonEmpty) {
    ASSERT_NE(version(), nullptr);
    EXPECT_GT(std::char_traits<char>::length(version()), 0u);
}

TEST(Types, SizeAndOpposite) {
    EXPECT_EQ(sizeof(Price), 8u);
    EXPECT_EQ(sizeof(Qty), 8u);
    EXPECT_EQ(opposite(Side::Buy), Side::Sell);
    EXPECT_EQ(opposite(Side::Sell), Side::Buy);
}

TEST(Reject, InvalidQty) {
    MatchingEngine eng;
    auto r = eng.submit(Side::Buy, OrderType::Limit, 100, 0);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::InvalidQty);
    r = eng.submit(Side::Buy, OrderType::Limit, 100, -5);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::InvalidQty);
}

TEST(Reject, InvalidPriceOnLimit) {
    MatchingEngine eng;
    auto r = eng.submit(Side::Buy, OrderType::Limit, 0, 10);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::InvalidPrice);
    r = eng.submit(Side::Buy, OrderType::Limit, -1, 10);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::InvalidPrice);
}

TEST(Reject, MarketIgnoresPrice) {
    MatchingEngine eng;
    auto r = eng.submit(Side::Buy, OrderType::Market, 0, 10);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->filled, 0);
    EXPECT_FALSE(r->resting);
}

TEST(Rest, NonCrossingLimits) {
    MatchingEngine eng;
    auto bid = eng.submit(Side::Buy, OrderType::Limit, 100, 10);
    auto ask = eng.submit(Side::Sell, OrderType::Limit, 101, 7);
    ASSERT_TRUE(bid);
    ASSERT_TRUE(ask);
    EXPECT_TRUE(bid->resting);
    EXPECT_TRUE(ask->resting);
    EXPECT_EQ(bid->filled, 0);
    EXPECT_EQ(ask->filled, 0);
    EXPECT_EQ(eng.best_bid(), 100);
    EXPECT_EQ(eng.best_ask(), 101);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 10);
    EXPECT_EQ(eng.quantity_at(Side::Sell, 101), 7);
    EXPECT_EQ(eng.resting_orders(), 2u);
    EXPECT_TRUE(eng.last_trades().empty());
}

TEST(Match, FullFillAtMakerPrice) {
    MatchingEngine eng;
    auto maker = eng.submit(Side::Sell, OrderType::Limit, 100, 5);
    auto taker = eng.submit(Side::Buy, OrderType::Limit, 105, 5);
    ASSERT_TRUE(maker);
    ASSERT_TRUE(taker);
    EXPECT_FALSE(taker->resting);
    EXPECT_EQ(taker->filled, 5);
    EXPECT_EQ(taker->remaining, 0);
    ASSERT_EQ(eng.last_trades().size(), 1u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, maker->order_id);
    EXPECT_EQ(eng.last_trades()[0].taker_id, taker->order_id);
    EXPECT_EQ(eng.last_trades()[0].price, 100);  // maker price, not 105
    EXPECT_EQ(eng.last_trades()[0].qty, 5);
    EXPECT_FALSE(eng.best_bid().has_value());
    EXPECT_FALSE(eng.best_ask().has_value());
    EXPECT_EQ(eng.resting_orders(), 0u);
}

TEST(Match, PartialFillRestsRemainder) {
    MatchingEngine eng;
    auto maker = eng.submit(Side::Sell, OrderType::Limit, 50, 3);
    auto taker = eng.submit(Side::Buy, OrderType::Limit, 50, 10);
    ASSERT_TRUE(taker);
    EXPECT_TRUE(taker->resting);
    EXPECT_EQ(taker->filled, 3);
    EXPECT_EQ(taker->remaining, 7);
    EXPECT_EQ(eng.best_bid(), 50);
    EXPECT_FALSE(eng.best_ask().has_value());
    EXPECT_EQ(eng.quantity_at(Side::Buy, 50), 7);
    EXPECT_EQ(eng.resting_orders(), 1u);
    (void)maker;
}

TEST(Match, PriceTimeFifoAtSameLevel) {
    MatchingEngine eng;
    auto a = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
    auto b = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
    auto c = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
    auto sell = eng.submit(Side::Sell, OrderType::Limit, 100, 2);
    ASSERT_TRUE(sell);
    EXPECT_EQ(sell->filled, 2);
    ASSERT_EQ(eng.last_trades().size(), 2u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, a->order_id);
    EXPECT_EQ(eng.last_trades()[1].maker_id, b->order_id);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 1);
    EXPECT_EQ(eng.resting_orders(), 1u);
    (void)c;
}

TEST(Match, BetterPriceFirstEvenIfLater) {
    MatchingEngine eng;
    auto low = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
    auto high = eng.submit(Side::Buy, OrderType::Limit, 101, 1);
    auto sell = eng.submit(Side::Sell, OrderType::Limit, 100, 1);
    ASSERT_EQ(eng.last_trades().size(), 1u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, high->order_id);
    EXPECT_EQ(eng.last_trades()[0].price, 101);
    EXPECT_EQ(eng.best_bid(), 100);
    EXPECT_EQ(eng.resting_orders(), 1u);
    (void)low;
    (void)sell;
}

TEST(Match, WalksMultipleLevels) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 100, 2));
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 101, 2));
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 102, 2));
    auto buy = eng.submit(Side::Buy, OrderType::Limit, 101, 5);
    ASSERT_TRUE(buy);
    EXPECT_EQ(buy->filled, 4);
    EXPECT_TRUE(buy->resting);
    EXPECT_EQ(buy->remaining, 1);
    EXPECT_EQ(eng.last_trades().size(), 2u);
    EXPECT_EQ(eng.last_trades()[0].price, 100);
    EXPECT_EQ(eng.last_trades()[1].price, 101);
    EXPECT_EQ(eng.best_ask(), 102);
    EXPECT_EQ(eng.best_bid(), 101);
}

TEST(Market, WalksUntilFilled) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 10, 2));
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 11, 2));
    auto mkt = eng.submit(Side::Buy, OrderType::Market, 0, 3);
    ASSERT_TRUE(mkt);
    EXPECT_EQ(mkt->filled, 3);
    EXPECT_FALSE(mkt->resting);
    EXPECT_EQ(eng.quantity_at(Side::Sell, 11), 1);
    EXPECT_EQ(eng.best_ask(), 11);
}

TEST(Market, NoLiquidityLeavesNothingResting) {
    MatchingEngine eng;
    auto mkt = eng.submit(Side::Sell, OrderType::Market, 0, 8);
    ASSERT_TRUE(mkt);
    EXPECT_EQ(mkt->filled, 0);
    EXPECT_EQ(mkt->remaining, 8);
    EXPECT_FALSE(mkt->resting);
    EXPECT_EQ(eng.resting_orders(), 0u);
}

TEST(Market, PartialThenCancelLeftover) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Buy, OrderType::Limit, 40, 4));
    auto mkt = eng.submit(Side::Sell, OrderType::Market, 0, 10);
    ASSERT_TRUE(mkt);
    EXPECT_EQ(mkt->filled, 4);
    EXPECT_EQ(mkt->remaining, 6);
    EXPECT_FALSE(mkt->resting);
    EXPECT_EQ(eng.resting_orders(), 0u);
}

TEST(Cancel, RestingOrder) {
    MatchingEngine eng;
    auto bid = eng.submit(Side::Buy, OrderType::Limit, 90, 4);
    auto ok = eng.cancel(bid->order_id);
    ASSERT_TRUE(ok);
    EXPECT_FALSE(eng.best_bid().has_value());
    EXPECT_EQ(eng.resting_orders(), 0u);
}

TEST(Cancel, NotFound) {
    MatchingEngine eng;
    auto r = eng.cancel(42);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::NotFound);
}

TEST(Cancel, MiddleOfFifo) {
    MatchingEngine eng;
    auto a = eng.submit(Side::Sell, OrderType::Limit, 70, 1);
    auto b = eng.submit(Side::Sell, OrderType::Limit, 70, 1);
    auto c = eng.submit(Side::Sell, OrderType::Limit, 70, 1);
    ASSERT_TRUE(eng.cancel(b->order_id));
    auto buy = eng.submit(Side::Buy, OrderType::Limit, 70, 2);
    ASSERT_EQ(eng.last_trades().size(), 2u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, a->order_id);
    EXPECT_EQ(eng.last_trades()[1].maker_id, c->order_id);
    EXPECT_EQ(eng.resting_orders(), 0u);
    EXPECT_FALSE(buy->resting);
}

TEST(Modify, QtyDownKeepsPriority) {
    MatchingEngine eng;
    auto a = eng.submit(Side::Buy, OrderType::Limit, 100, 5);
    auto b = eng.submit(Side::Buy, OrderType::Limit, 100, 5);
    auto m = eng.modify(a->order_id, 100, 2);
    ASSERT_TRUE(m);
    EXPECT_TRUE(m->resting);
    EXPECT_EQ(m->remaining, 2);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 7);
    auto sell = eng.submit(Side::Sell, OrderType::Limit, 100, 2);
    ASSERT_EQ(eng.last_trades().size(), 1u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, a->order_id);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 5);
    (void)b;
    (void)sell;
}

TEST(Modify, QtyUpLosesTimePriority) {
    MatchingEngine eng;
    auto a = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
    auto b = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
    auto m = eng.modify(a->order_id, 100, 3);
    ASSERT_TRUE(m);
    EXPECT_TRUE(m->resting);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 4);
    auto sell = eng.submit(Side::Sell, OrderType::Limit, 100, 1);
    ASSERT_EQ(eng.last_trades().size(), 1u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, b->order_id) << "qty-up must go to back of FIFO";
}

TEST(Modify, AggressivePriceMatches) {
    MatchingEngine eng;
    auto ask = eng.submit(Side::Sell, OrderType::Limit, 110, 4);
    auto bid = eng.submit(Side::Buy, OrderType::Limit, 100, 4);
    auto m = eng.modify(bid->order_id, 110, 4);
    ASSERT_TRUE(m);
    EXPECT_FALSE(m->resting);
    EXPECT_EQ(m->filled, 4);
    EXPECT_EQ(eng.resting_orders(), 0u);
    ASSERT_EQ(eng.last_trades().size(), 1u);
    EXPECT_EQ(eng.last_trades()[0].price, 110);
    (void)ask;
}

TEST(Modify, NotFoundAndInvalid) {
    MatchingEngine eng;
    auto r = eng.modify(99, 100, 1);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::NotFound);
    auto o = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
    r = eng.modify(o->order_id, 100, 0);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::InvalidQty);
    r = eng.modify(o->order_id, 0, 1);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::InvalidPrice);
}

TEST(Book, NeverCrosses) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Buy, OrderType::Limit, 100, 5));
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 100, 5));
    EXPECT_FALSE(eng.best_bid().has_value());
    EXPECT_FALSE(eng.best_ask().has_value());
    ASSERT_TRUE(eng.submit(Side::Buy, OrderType::Limit, 99, 1));
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 101, 1));
    auto top = eng.top();
    ASSERT_TRUE(top.has_bid);
    ASSERT_TRUE(top.has_ask);
    EXPECT_LT(top.bid_price, top.ask_price);
}

TEST(Book, BboAfterPartialWalk) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 10, 1));
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 11, 5));
    ASSERT_TRUE(eng.submit(Side::Buy, OrderType::Limit, 11, 1));
    EXPECT_EQ(eng.best_ask(), 11);
    EXPECT_EQ(eng.top().ask_qty, 5);
    EXPECT_FALSE(eng.best_bid().has_value());
}

TEST(Book, SymmetricSellTaker) {
    MatchingEngine eng;
    auto b1 = eng.submit(Side::Buy, OrderType::Limit, 50, 2);
    auto b2 = eng.submit(Side::Buy, OrderType::Limit, 49, 2);
    auto sell = eng.submit(Side::Sell, OrderType::Limit, 49, 3);
    EXPECT_EQ(sell->filled, 3);
    EXPECT_EQ(sell->remaining, 0);
    EXPECT_FALSE(sell->resting);
    EXPECT_EQ(eng.last_trades()[0].price, 50);
    EXPECT_EQ(eng.last_trades()[1].price, 49);
    EXPECT_EQ(eng.best_bid(), 49);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 49), 1);
    (void)b1;
    (void)b2;
}

TEST(Pool, ReusesNodesAfterCancel) {
    MatchingEngine eng(64, 8);
    std::vector<OrderId> ids;
    for (int i = 0; i < 32; ++i) {
        auto r = eng.submit(Side::Buy, OrderType::Limit, 100 + i % 3, 1);
        ids.push_back(r->order_id);
    }
    const auto mem_full = eng.memory_bytes();
    for (auto id : ids) {
        ASSERT_TRUE(eng.cancel(id));
    }
    EXPECT_EQ(eng.resting_orders(), 0u);
    for (int i = 0; i < 32; ++i) {
        auto r = eng.submit(Side::Sell, OrderType::Limit, 200 + i % 3, 1);
        ASSERT_TRUE(r->resting);
    }
    EXPECT_EQ(eng.resting_orders(), 32u);
    EXPECT_LE(eng.memory_bytes(), mem_full + 4096);
}

TEST(IntegerTicks, NoFractionalPrices) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 123456789, 1));
    auto buy = eng.submit(Side::Buy, OrderType::Limit, 123456789, 1);
    ASSERT_EQ(eng.last_trades()[0].price, 123456789);
    EXPECT_EQ(buy->filled, 1);
}
