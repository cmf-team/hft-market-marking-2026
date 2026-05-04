#include "bt/exchange.hpp"
#include "bt/order.hpp"
#include "bt/order_book.hpp"
#include "bt/static_quoter.hpp"
#include "bt/types.hpp"

#include <gtest/gtest.h>

#include <variant>
#include <vector>

namespace {

// Fake exchange that records the calls so tests can assert the quoter's
// behavior in isolation, without spinning up the matcher / latency layer.
class RecordingExchange final : public bt::IExchange {
public:
    struct Submit { bt::Timestamp now; bt::Side side; bt::Price price; bt::Qty qty; };
    struct Cancel { bt::Timestamp now; bt::OrderId id; };

    std::vector<Submit> submits;
    std::vector<Cancel> cancels;

    void post_only_limit(bt::Timestamp now, bt::Side side, bt::Price price, bt::Qty qty) override {
        submits.push_back({ now, side, price, qty });
    }
    void cancel(bt::Timestamp now, bt::OrderId id) override {
        cancels.push_back({ now, id });
    }
};

bt::OrderBook book_with(bt::Price bid_top, bt::Price ask_top) {
    bt::BookSnapshot s{};
    s.ts = 0;
    s.bids[0] = { bid_top, 100 };
    s.asks[0] = { ask_top, 100 };
    bt::OrderBook b;
    b.apply(s);
    return b;
}

TEST(StaticQuoter, PostsBothSidesOnFirstBook) {
    RecordingExchange ex;
    bt::StaticQuoter q(/*size=*/5);
    q.set_exchange(&ex);

    const auto book = book_with(100, 200);
    q.on_book(book, /*now=*/1000);

    ASSERT_EQ(ex.submits.size(), 2u);
    EXPECT_EQ(ex.submits[0].side,  bt::Side::Buy);
    EXPECT_EQ(ex.submits[0].price, 99);   // best_bid - 1
    EXPECT_EQ(ex.submits[0].qty,   5);
    EXPECT_EQ(ex.submits[1].side,  bt::Side::Sell);
    EXPECT_EQ(ex.submits[1].price, 201);  // best_ask + 1

    EXPECT_EQ(q.intended_buy_px(),  99);
    EXPECT_EQ(q.intended_sell_px(), 201);
    EXPECT_EQ(q.resting_buy_id(),  0u);
    EXPECT_EQ(q.resting_sell_id(), 0u);
}

TEST(StaticQuoter, OnSubmittedAcksAreAssignedFifoBySide) {
    RecordingExchange ex;
    bt::StaticQuoter q(5);
    q.set_exchange(&ex);

    q.on_book(book_with(100, 200), 1000);
    // First ack → buy, second ack → sell (FIFO).
    q.on_submitted(/*id=*/42);
    q.on_submitted(/*id=*/43);

    EXPECT_EQ(q.resting_buy_id(),  42u);
    EXPECT_EQ(q.resting_sell_id(), 43u);
}

TEST(StaticQuoter, NoRequoteWhenInsideUnchanged) {
    RecordingExchange ex;
    bt::StaticQuoter q(5);
    q.set_exchange(&ex);

    const auto book = book_with(100, 200);
    q.on_book(book, 1000);
    q.on_submitted(42);
    q.on_submitted(43);
    ex.submits.clear();
    ex.cancels.clear();

    q.on_book(book, 2000);
    EXPECT_TRUE(ex.submits.empty());
    EXPECT_TRUE(ex.cancels.empty());
}

TEST(StaticQuoter, RequotesWhenInsideMoves) {
    RecordingExchange ex;
    bt::StaticQuoter q(5);
    q.set_exchange(&ex);

    q.on_book(book_with(100, 200), 1000);
    q.on_submitted(42);  // buy id
    q.on_submitted(43);  // sell id
    ex.submits.clear();
    ex.cancels.clear();

    // Inside moves up: best bid 101, best ask 201. Both desired prices change.
    q.on_book(book_with(101, 201), 2000);

    // Cancels both old orders, posts both new ones.
    ASSERT_EQ(ex.cancels.size(), 2u);
    EXPECT_EQ(ex.cancels[0].id, 42u);
    EXPECT_EQ(ex.cancels[1].id, 43u);
    ASSERT_EQ(ex.submits.size(), 2u);
    EXPECT_EQ(ex.submits[0].side, bt::Side::Buy);
    EXPECT_EQ(ex.submits[0].price, 100);
    EXPECT_EQ(ex.submits[1].side, bt::Side::Sell);
    EXPECT_EQ(ex.submits[1].price, 202);
}

TEST(StaticQuoter, WaitsForAckBeforeRepostingSameSide) {
    RecordingExchange ex;
    bt::StaticQuoter q(5);
    q.set_exchange(&ex);

    // First book: posts buy+sell. We never deliver the on_submitted acks.
    q.on_book(book_with(100, 200), 1000);
    ex.submits.clear();

    // Inside moves; quoter must NOT issue duplicate submits while pending.
    q.on_book(book_with(101, 201), 2000);
    EXPECT_TRUE(ex.submits.empty());
    EXPECT_TRUE(ex.cancels.empty());
}

TEST(StaticQuoter, FillClearsSideAndAllowsImmediateRequote) {
    RecordingExchange ex;
    bt::StaticQuoter q(5);
    q.set_exchange(&ex);

    q.on_book(book_with(100, 200), 1000);
    q.on_submitted(42);
    q.on_submitted(43);
    ex.submits.clear();

    // Buy gets filled.
    q.on_fill(bt::Fill{ /*id=*/42, /*ts=*/1500, /*price=*/99, /*qty=*/5, bt::Side::Buy });
    EXPECT_EQ(q.resting_buy_id(), 0u);
    EXPECT_EQ(q.intended_buy_px(), 0);

    // Same book on the next tick → quoter reposts the buy side; sell stays.
    q.on_book(book_with(100, 200), 2000);
    ASSERT_EQ(ex.submits.size(), 1u);
    EXPECT_EQ(ex.submits[0].side,  bt::Side::Buy);
    EXPECT_EQ(ex.submits[0].price, 99);
}

TEST(StaticQuoter, RejectClearsPendingAndAllowsRetry) {
    RecordingExchange ex;
    bt::StaticQuoter q(5);
    q.set_exchange(&ex);

    q.on_book(book_with(100, 200), 1000);
    // Reject the buy submit (FIFO front), ack the sell submit.
    q.on_reject(bt::OrderReject{ /*id=*/0, /*ts=*/1100, bt::RejectReason::WouldCross });
    q.on_submitted(43);  // sell ack

    EXPECT_EQ(q.resting_buy_id(),   0u);
    EXPECT_EQ(q.intended_buy_px(),  0);
    EXPECT_EQ(q.resting_sell_id(), 43u);

    // Next tick the quoter retries the buy side from a fresh state.
    ex.submits.clear();
    q.on_book(book_with(100, 200), 2000);
    ASSERT_EQ(ex.submits.size(), 1u);
    EXPECT_EQ(ex.submits[0].side,  bt::Side::Buy);
    EXPECT_EQ(ex.submits[0].price, 99);
}

TEST(StaticQuoter, EmptyBookIsNoOp) {
    RecordingExchange ex;
    bt::StaticQuoter q(5);
    q.set_exchange(&ex);

    bt::OrderBook empty;
    q.on_book(empty, 1000);
    EXPECT_TRUE(ex.submits.empty());
    EXPECT_TRUE(ex.cancels.empty());
}

}  // namespace
