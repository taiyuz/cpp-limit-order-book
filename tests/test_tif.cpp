#include <gtest/gtest.h>

#include "lob/engine.hpp"
#include "lob/types.hpp"

using namespace lob;

TEST(Types, TimeInForceAndWouldNotFill) {
    EXPECT_EQ(to_string(TimeInForce::Gtc), "gtc");
    EXPECT_EQ(to_string(TimeInForce::Ioc), "ioc");
    EXPECT_EQ(to_string(TimeInForce::Fok), "fok");
    EXPECT_EQ(to_string(Error::WouldNotFill), "would_not_fill");
}

TEST(Gtc, ExplicitTifStillRestsRemainder) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 50, 3));
    auto taker = eng.submit(Side::Buy, OrderType::Limit, 50, 10, TimeInForce::Gtc);
    ASSERT_TRUE(taker);
    EXPECT_TRUE(taker->resting) << "GTC must rest leftover; IOC would cancel it";
    EXPECT_EQ(taker->filled, 3);
    EXPECT_EQ(taker->remaining, 7);
    EXPECT_EQ(eng.best_bid(), 50);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 50), 7);
    EXPECT_EQ(eng.resting_orders(), 1u);
}

TEST(Ioc, PartialDoesNotRest) {
    MatchingEngine eng;
    auto maker = eng.submit(Side::Sell, OrderType::Limit, 50, 3);
    auto taker = eng.submit(Side::Buy, OrderType::Limit, 50, 10, TimeInForce::Ioc);
    ASSERT_TRUE(taker);
    EXPECT_FALSE(taker->resting) << "IOC leftover must be cancelled, not GTC-rested";
    EXPECT_EQ(taker->filled, 3);
    EXPECT_EQ(taker->remaining, 7);
    EXPECT_FALSE(eng.best_bid().has_value());
    EXPECT_EQ(eng.resting_orders(), 0u);
    ASSERT_EQ(eng.last_trades().size(), 1u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, maker->order_id);
    EXPECT_EQ(eng.last_trades()[0].taker_id, taker->order_id);
    EXPECT_EQ(eng.last_trades()[0].price, 50);
    EXPECT_EQ(eng.last_trades()[0].qty, 3);
}

TEST(Ioc, DoesNotWalkThroughLimit) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 100, 2));
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 101, 5));
    auto ioc = eng.submit(Side::Buy, OrderType::Limit, 100, 10, TimeInForce::Ioc);
    ASSERT_TRUE(ioc);
    EXPECT_EQ(ioc->filled, 2) << "IOC is not a market order: it must stop at the limit";
    EXPECT_EQ(ioc->remaining, 8);
    EXPECT_FALSE(ioc->resting);
    EXPECT_EQ(eng.best_ask(), 101);
    EXPECT_EQ(eng.quantity_at(Side::Sell, 101), 5);
    EXPECT_EQ(eng.resting_orders(), 1u);
    ASSERT_EQ(eng.last_trades().size(), 1u);
    EXPECT_EQ(eng.last_trades()[0].price, 100);
}

TEST(Ioc, EmptyBookCancelsFully) {
    MatchingEngine eng;
    auto r = eng.submit(Side::Buy, OrderType::Limit, 100, 5, TimeInForce::Ioc);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->filled, 0);
    EXPECT_EQ(r->remaining, 5);
    EXPECT_FALSE(r->resting);
    EXPECT_EQ(eng.resting_orders(), 0u);
    EXPECT_TRUE(eng.last_trades().empty());
}

TEST(Ioc, FullFillLeavesNothing) {
    MatchingEngine eng;
    auto maker = eng.submit(Side::Buy, OrderType::Limit, 40, 4);
    auto ioc = eng.submit(Side::Sell, OrderType::Limit, 40, 4, TimeInForce::Ioc);
    ASSERT_TRUE(ioc);
    EXPECT_EQ(ioc->filled, 4);
    EXPECT_EQ(ioc->remaining, 0);
    EXPECT_FALSE(ioc->resting);
    EXPECT_EQ(eng.resting_orders(), 0u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, maker->order_id);
    EXPECT_EQ(eng.last_trades()[0].price, 40);
}

TEST(Ioc, SellWalksBidsAndCancelsLeftover) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Buy, OrderType::Limit, 50, 2));
    ASSERT_TRUE(eng.submit(Side::Buy, OrderType::Limit, 49, 2));
    auto ioc = eng.submit(Side::Sell, OrderType::Limit, 49, 10, TimeInForce::Ioc);
    ASSERT_TRUE(ioc);
    EXPECT_EQ(ioc->filled, 4);
    EXPECT_EQ(ioc->remaining, 6);
    EXPECT_FALSE(ioc->resting);
    EXPECT_EQ(eng.resting_orders(), 0u);
    ASSERT_EQ(eng.last_trades().size(), 2u);
    EXPECT_EQ(eng.last_trades()[0].price, 50);
    EXPECT_EQ(eng.last_trades()[1].price, 49);
}

TEST(Ioc, InvalidStillRejected) {
    MatchingEngine eng;
    auto r = eng.submit(Side::Buy, OrderType::Limit, 100, 0, TimeInForce::Ioc);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::InvalidQty);
    r = eng.submit(Side::Buy, OrderType::Limit, 0, 1, TimeInForce::Fok);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::InvalidPrice);
    EXPECT_EQ(eng.resting_orders(), 0u);
}

TEST(Fok, InsufficientLeavesMakersUntouched) {
    MatchingEngine eng;
    auto a = eng.submit(Side::Sell, OrderType::Limit, 100, 3);
    auto b = eng.submit(Side::Sell, OrderType::Limit, 101, 2);
    auto r = eng.submit(Side::Buy, OrderType::Limit, 100, 4, TimeInForce::Fok);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::WouldNotFill);
    EXPECT_TRUE(eng.last_trades().empty()) << "FOK reject must not emit partial fills";
    EXPECT_EQ(eng.resting_orders(), 2u);
    EXPECT_EQ(eng.quantity_at(Side::Sell, 100), 3);
    EXPECT_EQ(eng.quantity_at(Side::Sell, 101), 2);
    EXPECT_EQ(eng.best_ask(), 100);

    auto take = eng.submit(Side::Buy, OrderType::Limit, 101, 5);
    ASSERT_TRUE(take);
    EXPECT_EQ(take->filled, 5);
    EXPECT_EQ(eng.last_trades()[0].maker_id, a->order_id);
    EXPECT_EQ(eng.last_trades()[1].maker_id, b->order_id);
}

TEST(Fok, DoesNotCountNonCrossingLiquidity) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 100, 5));
    ASSERT_TRUE(eng.submit(Side::Sell, OrderType::Limit, 110, 100));
    auto r = eng.submit(Side::Buy, OrderType::Limit, 100, 6, TimeInForce::Fok);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::WouldNotFill)
        << "asks above the limit must not count toward FOK coverage";
    EXPECT_EQ(eng.quantity_at(Side::Sell, 100), 5);
    EXPECT_EQ(eng.quantity_at(Side::Sell, 110), 100);
    EXPECT_EQ(eng.resting_orders(), 2u);
    EXPECT_TRUE(eng.last_trades().empty());
}

TEST(Fok, CoveredWalksLevelsAtMakerPrices) {
    MatchingEngine eng;
    auto a = eng.submit(Side::Sell, OrderType::Limit, 100, 2);
    auto b = eng.submit(Side::Sell, OrderType::Limit, 101, 2);
    auto r = eng.submit(Side::Buy, OrderType::Limit, 101, 4, TimeInForce::Fok);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->filled, 4);
    EXPECT_EQ(r->remaining, 0);
    EXPECT_FALSE(r->resting);
    ASSERT_EQ(eng.last_trades().size(), 2u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, a->order_id);
    EXPECT_EQ(eng.last_trades()[0].price, 100);
    EXPECT_EQ(eng.last_trades()[0].qty, 2);
    EXPECT_EQ(eng.last_trades()[1].maker_id, b->order_id);
    EXPECT_EQ(eng.last_trades()[1].price, 101);
    EXPECT_EQ(eng.last_trades()[1].qty, 2);
    EXPECT_EQ(eng.resting_orders(), 0u);
}

TEST(Fok, ExactQtyAtOneLevel) {
    MatchingEngine eng;
    auto maker = eng.submit(Side::Buy, OrderType::Limit, 75, 8);
    auto r = eng.submit(Side::Sell, OrderType::Limit, 75, 8, TimeInForce::Fok);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->filled, 8);
    EXPECT_FALSE(r->resting);
    EXPECT_EQ(eng.last_trades()[0].maker_id, maker->order_id);
    EXPECT_EQ(eng.last_trades()[0].price, 75);
    EXPECT_EQ(eng.resting_orders(), 0u);
}

TEST(Fok, SellSideInsufficient) {
    MatchingEngine eng;
    auto bid = eng.submit(Side::Buy, OrderType::Limit, 50, 2);
    auto r = eng.submit(Side::Sell, OrderType::Limit, 50, 3, TimeInForce::Fok);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::WouldNotFill);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 50), 2);
    EXPECT_EQ(eng.resting_orders(), 1u);
    EXPECT_TRUE(eng.last_trades().empty());
    (void)bid;
}

TEST(Fok, EmptyBookRejects) {
    MatchingEngine eng;
    auto r = eng.submit(Side::Buy, OrderType::Limit, 100, 5, TimeInForce::Fok);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), Error::WouldNotFill);
    EXPECT_EQ(eng.resting_orders(), 0u);
    EXPECT_TRUE(eng.last_trades().empty());
}

TEST(Fok, MarketNeedsFullOppositeBook) {
    MatchingEngine eng;
    ASSERT_TRUE(eng.submit(Side::Buy, OrderType::Limit, 40, 4));
    auto short_mkt = eng.submit(Side::Sell, OrderType::Market, 0, 10, TimeInForce::Fok);
    ASSERT_FALSE(short_mkt);
    EXPECT_EQ(short_mkt.error(), Error::WouldNotFill);
    EXPECT_EQ(eng.quantity_at(Side::Buy, 40), 4) << "market FOK must not take a partial";
    EXPECT_EQ(eng.resting_orders(), 1u);

    auto ok = eng.submit(Side::Sell, OrderType::Market, 0, 4, TimeInForce::Fok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok->filled, 4);
    EXPECT_FALSE(ok->resting);
    EXPECT_EQ(eng.resting_orders(), 0u);
}

TEST(Fok, FifoAmongMakersOnCoveredFill) {
    MatchingEngine eng;
    auto a = eng.submit(Side::Sell, OrderType::Limit, 90, 1);
    auto b = eng.submit(Side::Sell, OrderType::Limit, 90, 1);
    auto c = eng.submit(Side::Sell, OrderType::Limit, 90, 1);
    auto r = eng.submit(Side::Buy, OrderType::Limit, 90, 2, TimeInForce::Fok);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->filled, 2);
    ASSERT_EQ(eng.last_trades().size(), 2u);
    EXPECT_EQ(eng.last_trades()[0].maker_id, a->order_id);
    EXPECT_EQ(eng.last_trades()[1].maker_id, b->order_id);
    EXPECT_EQ(eng.quantity_at(Side::Sell, 90), 1);
    EXPECT_EQ(eng.resting_orders(), 1u);
    (void)c;
}
