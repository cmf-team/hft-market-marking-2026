#include "bt/event_stream.hpp"
#include "bt/csv_lob_loader.hpp"
#include "bt/csv_trade_loader.hpp"
#include "bt/types.hpp"
#include "lob_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr bt::InstrumentSpec kSpec{1e-7, 1.0};

class MergedEventStreamTest : public ::testing::Test {
protected:
    fs::path lob_path_;
    fs::path trade_path_;

    void SetUp() override {
        const std::string name =
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
        lob_path_   = fs::temp_directory_path() / ("bt_test_es_lob_"   + name + ".csv");
        trade_path_ = fs::temp_directory_path() / ("bt_test_es_trade_" + name + ".csv");
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

TEST_F(MergedEventStreamTest, YieldsAllEventsInChronologicalOrder) {
    // LOB at ts = 100, 300, 500
    write_lob(bt::testing::make_lob_row(0, 100) +
              bt::testing::make_lob_row(1, 300) +
              bt::testing::make_lob_row(2, 500));
    // Trades at ts = 50, 200, 400, 600
    write_trades(
        "0,50,buy,0.0110435,1\n"
        "1,200,sell,0.0110435,2\n"
        "2,400,buy,0.0110436,3\n"
        "3,600,sell,0.0110435,4\n"
    );

    bt::CsvLobLoader   lob(lob_path_.string(),   kSpec);
    bt::CsvTradeLoader trades(trade_path_.string(), kSpec);
    bt::MergedEventStream stream(lob, trades);

    std::vector<bt::Timestamp> seen;
    seen.reserve(7);
    bt::Event ev;
    while (stream.next(ev)) {
        seen.push_back(bt::event_ts(ev));
    }

    // 3 LOB rows + 4 trade rows = 7 events, strictly chronological.
    ASSERT_EQ(seen.size(), 7u);
    EXPECT_EQ(seen, (std::vector<bt::Timestamp>{50, 100, 200, 300, 400, 500, 600}));
}

TEST_F(MergedEventStreamTest, AlternatesCorrectlyForInterleavedTimestamps) {
    write_lob(bt::testing::make_lob_row(0, 10) +
              bt::testing::make_lob_row(1, 30));
    write_trades(
        "0,20,buy,0.0110435,1\n"
        "1,40,sell,0.0110435,1\n"
    );

    bt::CsvLobLoader   lob(lob_path_.string(),   kSpec);
    bt::CsvTradeLoader trades(trade_path_.string(), kSpec);
    bt::MergedEventStream stream(lob, trades);

    bt::Event ev;
    std::vector<int> source_order;  // 0 = snapshot, 1 = trade

    while (stream.next(ev)) {
        source_order.push_back(std::holds_alternative<bt::BookSnapshot>(ev) ? 0 : 1);
    }
    EXPECT_EQ(source_order, (std::vector<int>{0, 1, 0, 1}));
}

TEST_F(MergedEventStreamTest, EqualTimestampSnapshotComesFirst) {
    // Both sources fire at ts = 1000.
    write_lob(bt::testing::make_lob_row(0, 1000));
    write_trades("0,1000,buy,0.0110435,1\n");

    bt::CsvLobLoader   lob(lob_path_.string(),   kSpec);
    bt::CsvTradeLoader trades(trade_path_.string(), kSpec);
    bt::MergedEventStream stream(lob, trades);

    bt::Event ev;
    ASSERT_TRUE(stream.next(ev));
    EXPECT_TRUE(std::holds_alternative<bt::BookSnapshot>(ev))
        << "snapshot must come before trade on equal timestamp";
    EXPECT_EQ(bt::event_ts(ev), 1000);

    ASSERT_TRUE(stream.next(ev));
    EXPECT_TRUE(std::holds_alternative<bt::Trade>(ev));
    EXPECT_EQ(bt::event_ts(ev), 1000);

    ASSERT_FALSE(stream.next(ev));
}

TEST_F(MergedEventStreamTest, EmptyTradeStreamYieldsOnlySnapshots) {
    write_lob(bt::testing::make_lob_row(0, 100) +
              bt::testing::make_lob_row(1, 200));
    write_trades("");  // header only, no rows

    bt::CsvLobLoader   lob(lob_path_.string(),   kSpec);
    bt::CsvTradeLoader trades(trade_path_.string(), kSpec);
    bt::MergedEventStream stream(lob, trades);

    bt::Event ev;
    std::size_t snapshots = 0;
    std::size_t trades_seen = 0;
    while (stream.next(ev)) {
        if (std::holds_alternative<bt::BookSnapshot>(ev)) ++snapshots;
        else ++trades_seen;
    }
    EXPECT_EQ(snapshots, 2u);
    EXPECT_EQ(trades_seen, 0u);
}

TEST_F(MergedEventStreamTest, EmptyLobStreamYieldsOnlyTrades) {
    write_lob("");
    write_trades(
        "0,50,buy,0.0110435,1\n"
        "1,75,sell,0.0110435,1\n"
    );

    bt::CsvLobLoader   lob(lob_path_.string(),   kSpec);
    bt::CsvTradeLoader trades(trade_path_.string(), kSpec);
    bt::MergedEventStream stream(lob, trades);

    bt::Event ev;
    std::vector<bt::Timestamp> seen;
    while (stream.next(ev)) {
        EXPECT_TRUE(std::holds_alternative<bt::Trade>(ev));
        seen.push_back(bt::event_ts(ev));
    }
    EXPECT_EQ(seen, (std::vector<bt::Timestamp>{50, 75}));
}

TEST_F(MergedEventStreamTest, BothEmptyReturnsFalseImmediately) {
    write_lob("");
    write_trades("");

    bt::CsvLobLoader   lob(lob_path_.string(),   kSpec);
    bt::CsvTradeLoader trades(trade_path_.string(), kSpec);
    bt::MergedEventStream stream(lob, trades);

    bt::Event ev;
    EXPECT_FALSE(stream.next(ev));
}

}  // namespace
