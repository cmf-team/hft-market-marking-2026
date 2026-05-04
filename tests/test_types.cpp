#include "bt/types.hpp"

#include <gtest/gtest.h>

namespace {

constexpr bt::InstrumentSpec kSampleSpec{1e-7, 1.0};

TEST(Types, ToTicksRoundsExactValue) {
    EXPECT_EQ(bt::to_ticks(0.0110435, kSampleSpec), 110435);
    EXPECT_EQ(bt::to_ticks(0.0110436, kSampleSpec), 110436);
    EXPECT_EQ(bt::to_ticks(0.0000001, kSampleSpec), 1);
    EXPECT_EQ(bt::to_ticks(0.0, kSampleSpec), 0);
}

TEST(Types, ToTicksHandlesFloatEpsilon) {
    // 0.0110435 cannot be exactly represented in binary64. Verify the helper
    // still snaps it to the right tick rather than truncating to 110434.
    const double noisy = 0.0110435 + 1e-18;
    EXPECT_EQ(bt::to_ticks(noisy, kSampleSpec), 110435);
}

TEST(Types, FromTicksRoundTrip) {
    for (bt::Price t : {0L, 1L, 110435L, 999999L, 12345678L}) {
        const double p = bt::from_ticks(t, kSampleSpec);
        EXPECT_EQ(bt::to_ticks(p, kSampleSpec), t) << "ticks=" << t;
    }
}

TEST(Types, IsOnTickGridAcceptsAlignedPrices) {
    EXPECT_TRUE(bt::is_on_tick_grid(0.0110435, kSampleSpec));
    EXPECT_TRUE(bt::is_on_tick_grid(0.0, kSampleSpec));
    EXPECT_TRUE(bt::is_on_tick_grid(0.0000001, kSampleSpec));
}

TEST(Types, IsOnTickGridRejectsOffGridPrices) {
    // Halfway between two ticks for tick_size = 1e-7.
    EXPECT_FALSE(bt::is_on_tick_grid(0.01104355, kSampleSpec));
    // A small but unambiguous off-grid offset (10% of a tick).
    EXPECT_FALSE(bt::is_on_tick_grid(0.0110435 + 1e-8, kSampleSpec));
}

TEST(Types, QtyConversionRoundTrip) {
    EXPECT_EQ(bt::to_qty(734.0, kSampleSpec), 734);
    EXPECT_EQ(bt::to_qty(25445.0, kSampleSpec), 25445);
    EXPECT_DOUBLE_EQ(bt::from_qty(734, kSampleSpec), 734.0);
}

}  // namespace
