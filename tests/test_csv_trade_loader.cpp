#include "bt/csv_trade_loader.hpp"
#include "bt/types.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr bt::InstrumentSpec kSpec{1e-7, 1.0};

class CsvTradeLoaderTest : public ::testing::Test {
protected:
    fs::path path_;

    void SetUp() override {
        path_ = fs::temp_directory_path() /
                ("bt_test_trades_" +
                 std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                 "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                 ".csv");
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove(path_, ec);
    }

    void write(const std::string& content) {
        std::ofstream f(path_);
        f << content;
    }
};

TEST_F(CsvTradeLoaderTest, ParsesSampleRows) {
    write(
        ",local_timestamp,side,price,amount\n"
        "0,1722470400014926,sell,0.0110435,734\n"
        "1,1722470402982045,sell,0.0110435,1633\n"
        "4,1722470403047136,buy,0.0110436,5378\n"
    );

    bt::CsvTradeLoader loader(path_.string(), kSpec);
    bt::Trade t{};

    ASSERT_TRUE(loader.next(t));
    EXPECT_EQ(t.ts, 1722470400014926);
    EXPECT_EQ(t.side, bt::Side::Sell);
    EXPECT_EQ(t.price, 110435);
    EXPECT_EQ(t.amount, 734);

    ASSERT_TRUE(loader.next(t));
    EXPECT_EQ(t.ts, 1722470402982045);
    EXPECT_EQ(t.amount, 1633);

    ASSERT_TRUE(loader.next(t));
    EXPECT_EQ(t.side, bt::Side::Buy);
    EXPECT_EQ(t.price, 110436);
    EXPECT_EQ(t.amount, 5378);

    ASSERT_FALSE(loader.next(t));  // EOF
}

TEST_F(CsvTradeLoaderTest, RoundTripExactForOnGridPrices) {
    write(
        ",local_timestamp,side,price,amount\n"
        "0,1,buy,0.0110435,1\n"
        "1,2,sell,0.0110436,1\n"
        "2,3,buy,0.0000001,1\n"
    );

    bt::CsvTradeLoader loader(path_.string(), kSpec);
    bt::Trade t{};

    ASSERT_TRUE(loader.next(t));
    EXPECT_DOUBLE_EQ(bt::from_ticks(t.price, kSpec), 0.0110435);
    ASSERT_TRUE(loader.next(t));
    EXPECT_DOUBLE_EQ(bt::from_ticks(t.price, kSpec), 0.0110436);
    ASSERT_TRUE(loader.next(t));
    EXPECT_DOUBLE_EQ(bt::from_ticks(t.price, kSpec), 0.0000001);
}

TEST_F(CsvTradeLoaderTest, RejectsOffGridPrice) {
    write(
        ",local_timestamp,side,price,amount\n"
        "0,1,buy,0.01104355,1\n"  // halfway between two ticks
    );

    bt::CsvTradeLoader loader(path_.string(), kSpec);
    bt::Trade t{};
    EXPECT_THROW(loader.next(t), std::runtime_error);
}

TEST_F(CsvTradeLoaderTest, RejectsBadSide) {
    write(
        ",local_timestamp,side,price,amount\n"
        "0,1,short,0.0110435,1\n"
    );

    bt::CsvTradeLoader loader(path_.string(), kSpec);
    bt::Trade t{};
    EXPECT_THROW(loader.next(t), std::runtime_error);
}

TEST_F(CsvTradeLoaderTest, ThrowsOnMissingFile) {
    EXPECT_THROW(bt::CsvTradeLoader("/nonexistent/path.csv", kSpec),
                 std::runtime_error);
}

}  // namespace
