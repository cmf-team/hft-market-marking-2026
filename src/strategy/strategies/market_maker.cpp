#include "strategy/strategies/market_maker.hpp"

#include "book/order_book.hpp"

namespace cmf
{

void MarketMaker::on_book_update(const OrderBook& book)
{
    if (book.empty())
        return;

    constexpr Quantity kQuoteSize = 10'000.0;

    const PriceLevel bb = book.best_bid();
    const PriceLevel ba = book.best_ask();
    const Price half_spread = 0.5 * (ba.price - bb.price);

    bid_price_ = bb.price + half_spread;
    ask_price_ = ba.price - half_spread;
    bid_size_ = kQuoteSize;
    ask_size_ = kQuoteSize;
    active_ = true;
    ++requote_count_;

    if (me_)
    {
        const NanoTime ts = book.timestamp();
        me_->place(Side::Buy, bid_price_, bid_size_, ts);
        me_->place(Side::Sell, ask_price_, ask_size_, ts);
    }
}

} // namespace cmf
