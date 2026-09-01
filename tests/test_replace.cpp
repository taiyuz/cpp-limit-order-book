#include <gtest/gtest.h>

#include "lob/engine.hpp"
#include "lob/types.hpp"

using namespace lob;

TEST(Replace, NotFoundLeavesBookUnchanged) {
    MatchingEngine eng;
    auto bid = eng.submit(Side::Buy, OrderType::Limit, 100, 5);
    auto ask = eng.submit(Side::Sell, OrderType::Limit, 110, 3);
    ASSERT_TRUE(bid);
    ASSERT_TRUE(ask);
    auto r = eng.replace(999, 105, 2);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::NotFound);
    EXPECT_EQ(eng.resting_orders(), 2u);
    EXPECT_EQ(eng.best_bid(), 100);
    EXPECT_EQ(eng.best_ask(), 110);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 5);
    EXPECT_EQ(eng.quantity_at(Side::Sell, 110), 3);
    EXPECT_TRUE(eng.last_trades().empty());
}

TEST(Replace, InvalidParamsDoNotPull) {
    MatchingEngine eng;
    auto o = eng.submit(Side::Buy, OrderType::Limit, 100, 4);
    ASSERT_TRUE(o);
    auto r = eng.replace(o->order_id, 100, 0);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::InvalidQty);
    EXPECT_EQ(eng.resting_orders(), 1u);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 4);

    r = eng.replace(o->order_id, 0, 2);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::InvalidPrice);
    EXPECT_EQ(eng.resting_orders(), 1u);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 4);
    EXPECT_EQ(eng.best_bid(), 100);
}

TEST(Replace, NewIdAndLosesTimePriority) {
    MatchingEngine eng;
    auto a = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
    auto b = eng.submit(Side::Buy, OrderType::Limit, 100, 1);
    auto r = eng.replace(a->order_id, 100, 1);
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->resting);
    EXPECT_NE(r->order_id, a->order_id);
    EXPECT_EQ(r->filled, 0);
    EXPECT_EQ(r->remaining, 1);
    EXPECT_EQ(eng.resting_orders(), 2u);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 2);

    auto miss = eng.cancel(a->order_id);
    ASSERT_FALSE(miss);
    EXPECT_EQ(miss.error(), Error::NotFound);

    auto sell = eng.submit(Side::Sell, OrderType::Limit, 100, 1);
    ASSERT_EQ(eng.last_trades().size(), 1u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, b->order_id)
        << "replace must go to the back of the FIFO, unlike qty-down modify";
    EXPECT_EQ(eng.last_trades()[0].qty, 1);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 1);
    EXPECT_EQ(eng.resting_orders(), 1u);
    (void)sell;
}

TEST(Replace, AggressivePartialRestsRemainder) {
    MatchingEngine eng;
    auto ask = eng.submit(Side::Sell, OrderType::Limit, 110, 5);
    auto bid = eng.submit(Side::Buy, OrderType::Limit, 100, 10);
    auto r = eng.replace(bid->order_id, 110, 10);
    ASSERT_TRUE(r);
    EXPECT_NE(r->order_id, bid->order_id);
    EXPECT_EQ(r->filled, 5);
    EXPECT_EQ(r->remaining, 5);
    EXPECT_TRUE(r->resting);
    ASSERT_EQ(eng.last_trades().size(), 1u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, ask->order_id);
    EXPECT_EQ(eng.last_trades()[0].taker_id, r->order_id);
    EXPECT_EQ(eng.last_trades()[0].price, 110);
    EXPECT_EQ(eng.last_trades()[0].qty, 5);
    EXPECT_FALSE(eng.best_ask().has_value());
    EXPECT_EQ(eng.best_bid(), 110);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 110), 5);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 100), 0);
    EXPECT_EQ(eng.resting_orders(), 1u);

    const BookSnapshot snap = eng.snapshot();
    ASSERT_EQ(snap.bids.size(), 1u);
    ASSERT_EQ(snap.bids[0].orders.size(), 1u);
    EXPECT_EQ(snap.bids[0].orders[0].id, r->order_id);
    EXPECT_EQ(snap.bids[0].orders[0].remaining, 5);
}

TEST(Replace, SellSideKeepsSideAndCanCross) {
    MatchingEngine eng;
    auto bid = eng.submit(Side::Buy, OrderType::Limit, 50, 3);
    auto ask = eng.submit(Side::Sell, OrderType::Limit, 60, 8);
    auto r = eng.replace(ask->order_id, 50, 8);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->filled, 3);
    EXPECT_EQ(r->remaining, 5);
    EXPECT_TRUE(r->resting);
    EXPECT_EQ(eng.best_ask(), 50);
    EXPECT_FALSE(eng.best_bid().has_value());
    EXPECT_EQ(eng.quantity_at(Side::Sell, 50), 5);
    EXPECT_EQ(eng.last_trades()[0].price, 50);
    EXPECT_EQ(eng.last_trades()[0].maker_id, bid->order_id);
    EXPECT_EQ(eng.last_trades()[0].taker_id, r->order_id);
}

TEST(Replace, SamePriceQtyStillNewId) {
    MatchingEngine eng;
    auto o = eng.submit(Side::Sell, OrderType::Limit, 70, 4);
    auto r = eng.replace(o->order_id, 70, 4);
    ASSERT_TRUE(r);
    EXPECT_NE(r->order_id, o->order_id);
    EXPECT_TRUE(r->resting);
    EXPECT_EQ(r->remaining, 4);
    EXPECT_EQ(eng.resting_orders(), 1u);
    EXPECT_EQ(eng.best_ask(), 70);
    EXPECT_EQ(eng.quantity_at(Side::Sell, 70), 4);
}
