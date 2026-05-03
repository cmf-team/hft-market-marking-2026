// tests for TradeReader / LOBReader

#include "common/CsvReader.hpp"
#include "common/Types.hpp"

#include "TempFile.hpp"
#include "catch2/catch_all.hpp"

#include <fstream>

using namespace cmf;

TEST_CASE("TradeReader - parses sides and prices", "[CsvReader]")
{
    TempFile tmp("trades_test.csv");
    {
        std::ofstream out(tmp.getPath());
        out << ",local_timestamp,side,price,amount\n"
            << "0,1722470400000000,sell,0.0110435,734\n"
            << "1,1722470400500000,buy,0.0110436,500\n";
    }

    TradeReader r(tmp.getPath());
    Trade t;

    REQUIRE(r.next(t));
    REQUIRE(t.timestamp == 1722470400000000);
    REQUIRE(t.side == Side::Sell);
    REQUIRE(t.price == Catch::Approx(0.0110435));
    REQUIRE(t.amount == Catch::Approx(734.0));

    REQUIRE(r.next(t));
    REQUIRE(t.side == Side::Buy);
    REQUIRE(t.price == Catch::Approx(0.0110436));

    REQUIRE_FALSE(r.next(t));
}

TEST_CASE("LOBReader - parses 25-level snapshot", "[CsvReader]")
{
    TempFile tmp("lob_test.csv");
    {
        std::ofstream out(tmp.getPath());
        out << "header_ignored\n";
        out << "0,1722470402000000";
        for (int i = 0; i < kLobDepth; ++i)
        {
            double ap = 100.0 + i;
            double aa = 10.0 + i;
            double bp = 99.0 - i;
            double ba = 11.0 + i;
            out << "," << ap << "," << aa << "," << bp << "," << ba;
        }
        out << "\n";
    }

    LOBReader r(tmp.getPath());
    LOBSnapshot s;
    REQUIRE(r.next(s));
    REQUIRE(s.timestamp == 1722470402000000);
    REQUIRE(s.asks[0].price == Catch::Approx(100.0));
    REQUIRE(s.asks[0].amount == Catch::Approx(10.0));
    REQUIRE(s.bids[0].price == Catch::Approx(99.0));
    REQUIRE(s.bids[0].amount == Catch::Approx(11.0));
    REQUIRE(s.asks[24].price == Catch::Approx(124.0));
    REQUIRE(s.bids[24].amount == Catch::Approx(35.0));
    REQUIRE_FALSE(r.next(s));
}
