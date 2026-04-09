#include "engine/backtest_engine.hpp"

#include "bt/csv_lob_loader.hpp"
#include "bt/csv_trade_loader.hpp"
#include "bt/event_stream.hpp"
#include "bt/exchange.hpp"
#include "bt/latency_model.hpp"
#include "bt/order_book.hpp"
#include "bt/queue_model.hpp"
#include "bt/strategy.hpp"
#include "bt/types.hpp"

#include "lob_fixture.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr bt::InstrumentSpec kSpec{1e-7, 1.0};

// make_lob_row uses bid[0] = 110435 ticks, ask[0] = 110436 ticks, bid[0] amount=200.

class RecordingStrategy : public bt::IStrategy {
public:
    int                            books_seen   = 0;
    int                            trades_seen  = 0;
    std::vector<bt::OrderId>       submitted;
    std::vector<bt::Fill>          fills;
    std::vector<bt::OrderReject>   rejects;
    std::vector<bt::OrderId>       cancel_acks;
    std::vector<bt::CancelReject>  cancel_rejects;

    // Posts a passive buy on the very first on_book event, never again.
    bool      auto_post  = false;
    bt::Price post_price = 0;
    bt::Qty   post_qty   = 0;
    bool      posted_    = false;

    void on_book(const bt::OrderBook&, bt::Timestamp now) override {
        ++books_seen;
        if (auto_post && !posted_) {
            exchange_->post_only_limit(now, bt::Side::Buy, post_price, post_qty);
            posted_ = true;
        }
    }
    void on_trade(const bt::Trade&) override                       { ++trades_seen; }
    void on_submitted(bt::OrderId id) override                     { submitted.push_back(id); }
    void on_fill(const bt::Fill& f) override                       { fills.push_back(f); }
    void on_reject(const bt::OrderReject& r) override              { rejects.push_back(r); }
    void on_cancel_ack(bt::OrderId id) override                    { cancel_acks.push_back(id); }
    void on_cancel_reject(const bt::CancelReject& r) override      { cancel_rejects.push_back(r); }
};

class BacktestEngineTest : public ::testing::Test {
protected:
    fs::path lob_path_;
    fs::path trade_path_;

    void SetUp() override {
        const std::string name =
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
        lob_path_   = fs::temp_directory_path() / ("bt_test_engine_lob_"   + name + ".csv");
        trade_path_ = fs::temp_directory_path() / ("bt_test_engine_trade_" + name + ".csv");
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove(lob_path_,   ec);
        fs::remove(trade_path_, ec);
    }

    void write_lob(const std::string& body) {
        std::ofstream f(lob_path_);
        f << bt::testing::make_lob_header() << body;
    }

    void write_trades(const std::string& body) {
        std::ofstream f(trade_path_);
        f << ",local_timestamp,side,price,amount\n" << body;
    }
};

TEST_F(BacktestEngineTest, DispatchesAllEventsToStrategy) {
    write_lob(bt::testing::make_lob_row(0, 100) +
              bt::testing::make_lob_row(1, 300));
    write_trades(
        "0,200,buy,0.0110436,1\n"
        "1,400,sell,0.0110435,1\n");

    bt::CsvLobLoader     lob(lob_path_.string(), kSpec);
    bt::CsvTradeLoader   trd(trade_path_.string(), kSpec);
    bt::MergedEventStream stream(lob, trd);

    bt::PessimisticQueueModel qm;
    bt::FixedLatencyModel     lm(0, 0, 0);
    RecordingStrategy         strat;

    bt::BacktestEngine engine(stream, qm, lm, strat);
    const auto count = engine.run();

    EXPECT_EQ(count, 4);
    EXPECT_EQ(strat.books_seen,  2);
    EXPECT_EQ(strat.trades_seen, 2);
}

TEST_F(BacktestEngineTest, PostedOrderFillsViaTradeStreamUpdatesPortfolio) {
    // Two snapshots so the strategy gets a book event before any trade.
    write_lob(bt::testing::make_lob_row(0, 100) +
              bt::testing::make_lob_row(1, 500));
    // A sell trade at exactly our buy price 0.0110400 (110400 ticks) — outside
    // the book's posted bid levels (110411..110435). After our buy is resting
    // there with queue_ahead=0 (price wasn't a level), the trade fills it.
    write_trades("0,200,sell,0.0110400,3\n");

    bt::CsvLobLoader     lob(lob_path_.string(), kSpec);
    bt::CsvTradeLoader   trd(trade_path_.string(), kSpec);
    bt::MergedEventStream stream(lob, trd);

    bt::PessimisticQueueModel qm;
    bt::FixedLatencyModel     lm(0, 0, 0);
    RecordingStrategy         strat;
    strat.auto_post  = true;
    strat.post_price = 110400;
    strat.post_qty   = 3;

    bt::BacktestEngine engine(stream, qm, lm, strat);
    engine.run();

    // Order resting was filled fully.
    ASSERT_EQ(strat.fills.size(), 1u);
    EXPECT_EQ(strat.fills[0].qty, 3);
    EXPECT_EQ(strat.fills[0].price, 110400);
    EXPECT_EQ(strat.fills[0].side, bt::Side::Buy);

    // Strategy received the on_submitted ack as well.
    ASSERT_EQ(strat.submitted.size(), 1u);
    EXPECT_EQ(strat.fills[0].id, strat.submitted[0]);

    // Portfolio reflects the fill: long 3 @ 110400.
    EXPECT_EQ(engine.portfolio().position(),       3);
    EXPECT_EQ(engine.portfolio().avg_entry_price(), 110400);
    EXPECT_EQ(engine.portfolio().realized_pnl_ticks(), 0);
}

TEST_F(BacktestEngineTest, QueueModelPreventsOverFillBehindResting) {
    // Strategy posts at the existing best bid (110435) where the book already
    // has 200 lots resting. queue_ahead = 200. A 100-lot trade should not
    // fill our order — it only erodes the queue.
    write_lob(bt::testing::make_lob_row(0, 100) +
              bt::testing::make_lob_row(1, 500));
    write_trades("0,200,sell,0.0110435,100\n");

    bt::CsvLobLoader     lob(lob_path_.string(), kSpec);
    bt::CsvTradeLoader   trd(trade_path_.string(), kSpec);
    bt::MergedEventStream stream(lob, trd);

    bt::PessimisticQueueModel qm;
    bt::FixedLatencyModel     lm(0, 0, 0);
    RecordingStrategy         strat;
    strat.auto_post  = true;
    strat.post_price = 110435;
    strat.post_qty   = 5;

    bt::BacktestEngine engine(stream, qm, lm, strat);
    engine.run();

    EXPECT_TRUE(strat.fills.empty());
    EXPECT_EQ(engine.portfolio().position(), 0);
    // The order is still resting in the matcher.
    EXPECT_EQ(engine.matcher().resting_count(), 1u);
    // And the strategy did receive its submitted ack.
    EXPECT_EQ(strat.submitted.size(), 1u);
}

TEST_F(BacktestEngineTest, PostOnlyRejectFlowsThroughToStrategy) {
    write_lob(bt::testing::make_lob_row(0, 100));
    write_trades("");  // no trades

    bt::CsvLobLoader     lob(lob_path_.string(), kSpec);
    bt::CsvTradeLoader   trd(trade_path_.string(), kSpec);
    bt::MergedEventStream stream(lob, trd);

    bt::PessimisticQueueModel qm;
    bt::FixedLatencyModel     lm(0, 0, 0);
    RecordingStrategy         strat;
    // Buy at the best ask (110436) — would cross at delivery time.
    strat.auto_post  = true;
    strat.post_price = 110436;
    strat.post_qty   = 1;

    bt::BacktestEngine engine(stream, qm, lm, strat);
    engine.run();

    EXPECT_TRUE(strat.fills.empty());
    EXPECT_TRUE(strat.submitted.empty());
    ASSERT_EQ(strat.rejects.size(), 1u);
    EXPECT_EQ(strat.rejects[0].reason, bt::RejectReason::WouldCross);
    EXPECT_EQ(engine.matcher().resting_count(), 0u);
}

}  // namespace
