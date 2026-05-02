#pragma once
#include "bt_engine/data_reader/LimitOrderBookReader.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

struct Order {
    long long id = 0;
    long long timestamp = 0;
    double price = 0.0;
    double amount = 0.0;
    double filledAmount = 0.0;
    std::string side;       // "buy" or "sell"
    std::string eventType;  // "new", "cancel", "fill"
};

class StrategyBase {
protected:
    std::vector<Order> trades;
    double currentMid = 0.0;

public:
    virtual ~StrategyBase() = default;

    virtual std::vector<Order> reactToLob(const OrderBookRow& lob) = 0;

    virtual void reactToExecution(const std::vector<Order>& fills) {
        for (const Order& fill : fills)
            trades.push_back(fill);
    }

    void updateMid(double mid) { currentMid = mid; }

    // cash + mark-to-market of open position
    [[nodiscard]] double calculatePnl() const {
        double cash = 0.0, position = 0.0;
        for (const Order& t : trades) {
            if (t.side == "buy") { cash -= t.amount * t.price; position += t.amount; }
            else                 { cash += t.amount * t.price; position -= t.amount; }
        }
        return cash + position * currentMid;
    }

    // total notional traded
    [[nodiscard]] double calculateTurnover() const {
        double vol = 0.0;
        for (const Order& t : trades) vol += t.amount * t.price;
        return vol;
    }

    void writeCsv(const std::string& filename) const {
        std::ofstream f(filename);
        f << "timestamp,side,price,amount,position,cash_flow,mtm_pnl\n";
        double cash = 0.0, position = 0.0;
        for (const Order& t : trades) {
            if (t.side == "buy") { cash -= t.amount * t.price; position += t.amount; }
            else                 { cash += t.amount * t.price; position -= t.amount; }
            double mtm = cash + position * t.price;
            f << t.timestamp << "," << t.side << "," << t.price << ","
              << t.amount << "," << position << "," << cash << "," << mtm << "\n";
        }
    }

    void writeReport(const std::string& filename) const {
        double cash = 0.0, position = 0.0, turnover = 0.0;
        double peak = 0.0, maxDrawdown = 0.0;
        for (const Order& t : trades) {
            turnover += t.amount * t.price;
            if (t.side == "buy") { cash -= t.amount * t.price; position += t.amount; }
            else                 { cash += t.amount * t.price; position -= t.amount; }
            double mtm = cash + position * t.price;
            peak        = std::max(peak, mtm);
            maxDrawdown = std::max(maxDrawdown, peak - mtm);
        }
        std::ofstream f(filename);
        f << "Trades:      " << trades.size()                      << "\n"
          << "Final PnL:   " << cash + position * currentMid       << "\n"
          << "Position:    " << position                            << "\n"
          << "Turnover:    " << turnover                            << "\n"
          << "MaxDrawdown: " << maxDrawdown                         << "\n";
    }
};
