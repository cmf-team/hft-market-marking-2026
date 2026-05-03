#include "SimulationEngine.hpp"
#include <algorithm>
#include <cmath>

namespace cmf {

SimulationEngine::SimulationEngine(const std::string& lobFile,
                                   const std::string& tradeFile,
                                   const StratParams& params)
    : strategy_(params) {
    loadEvents(lobFile, tradeFile);
}

void SimulationEngine::loadEvents(const std::string& lobFile,
                                  const std::string& tradeFile) {
    // Load LOB
    {
        CsvReader csv(lobFile);
        std::vector<std::string> fields;
        while (csv.readRow(fields)) {
            if (fields.size() < 4) continue;
            LOBEvent ev;
            ev.timestamp = std::stoll(fields[0]);
            int s = std::stoi(fields[1]);
            ev.side = s > 0 ? Side::Buy : Side::Sell;
            ev.price = std::stod(fields[2]);
            ev.qty = std::stod(fields[3]);
            events_.push_back(ev);
        }
    }
    // Load trades
    {
        CsvReader csv(tradeFile);
        std::vector<std::string> fields;
        while (csv.readRow(fields)) {
            if (fields.size() < 4) continue;
            TradeEvent ev;
            ev.timestamp = std::stoll(fields[0]);
            ev.price = std::stod(fields[1]);
            ev.qty = std::stod(fields[2]);
            int s = std::stoi(fields[3]);
            ev.aggressor = s > 0 ? Side::Buy : Side::Sell;
            events_.push_back(ev);
        }
    }
    // Sort all events by timestamp
    std::sort(events_.begin(), events_.end(),
              [](const Event& a, const Event& b) {
                  return getTimestamp(a) < getTimestamp(b);
              });
}

SimResult SimulationEngine::run() {
    SimResult res;
    Quantity inventory = 0;
    double cash = 0;
    LimitOrderBook book;
    KalmanFilter kf(1e-7, 1e-5, 0.0, 1.0);
    kf.update(0); // initial state

    hasQuote_ = false;

    if (events_.empty()) return res;
    NanoTime startTime = getTimestamp(events_[0]);
    NanoTime endTime = startTime + static_cast<NanoTime>(
        strategy_.params().timeHorizonSec * 1'000'000'000LL);

    for (const auto& event : events_) {
        NanoTime now = getTimestamp(event);
        if (now >= endTime) break;

        double timeRem = static_cast<double>(endTime - now) / 1e9;

        // 1. Process event
        if (std::holds_alternative<LOBEvent>(event)) {
            const auto& lob = std::get<LOBEvent>(event);
            book.applyUpdate(lob);

            if (hasQuote_) {
                bool buyFilled = book.wouldBuyFill(currentQuote_.bid);
                bool sellFilled = book.wouldSellFill(currentQuote_.ask);

                if (buyFilled) {
                    Price fillPrice = book.bestAsk();
                    cash -= fillPrice * 1.0;
                    inventory += 1;
                    FillRecord rec = {now, Side::Buy, fillPrice, 1};
                    res.fills.push_back(rec);
                    hasQuote_ = false;
                }
                if (sellFilled) {
                    Price fillPrice = book.bestBid();
                    cash += fillPrice * 1.0;
                    inventory -= 1;
                    FillRecord rec = {now, Side::Sell, fillPrice, 1};
                    res.fills.push_back(rec);
                    hasQuote_ = false;
                }
            }
        }
        else if (std::holds_alternative<TradeEvent>(event)) {
            const auto& trade = std::get<TradeEvent>(event);
            if (hasQuote_) {
                if (currentQuote_.bid >= trade.price) {
                    cash -= trade.price * 1.0;
                    inventory += 1;
                    FillRecord rec = {now, Side::Buy, trade.price, 1};
                    res.fills.push_back(rec);
                    hasQuote_ = false;
                }
                else if (currentQuote_.ask <= trade.price) {
                    cash += trade.price * 1.0;
                    inventory -= 1;
                    FillRecord rec = {now, Side::Sell, trade.price, 1};
                    res.fills.push_back(rec);
                    hasQuote_ = false;
                }
            }
        }

        // 2. Update Kalman filter with current reference price
        Price reference = book.microPrice(0.2);
        kf.update(reference);
        Price predicted = kf.predict();

        // 3. Replace quote if none is pending and book is valid
        if (!hasQuote_ && book.bestBid() > 0 && book.bestAsk() > 0) {
            currentQuote_ = strategy_.computeQuotes(predicted, inventory, timeRem);
            hasQuote_ = true;
        }

        // 4. Mark‑to‑market PnL
        double mtm = cash + inventory * book.midPrice();
        res.inventoryPath.push_back(inventory);
        res.pnlPath.push_back(mtm);
    }

    res.finalPnL = res.pnlPath.empty() ? 0 : res.pnlPath.back();
    res.finalInventory = inventory;
    return res;
}

} // namespace cmf