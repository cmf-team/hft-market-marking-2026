#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Level {
    double price;
    double amount;
};

struct OrderBookRow {
    long long index;
    long long local_timestamp;
    std::vector<Level> asks;  // 25 уровней
    std::vector<Level> bids;  // 25 уровней
};

OrderBookRow parseLOBSnapshot(const std::string& line) {
    OrderBookRow row;
    row.asks.resize(25);
    row.bids.resize(25);

    std::stringstream ss(line);
    std::string token;

    // index
    std::getline(ss, token, ',');
    row.index = std::stoll(token);

    // timestamp
    std::getline(ss, token, ',');
    row.local_timestamp = std::stoll(token);

    for (int i = 0; i < 25; ++i) {
        std::getline(ss, token, ','); row.asks[i].price  = std::stod(token);
        std::getline(ss, token, ','); row.asks[i].amount = std::stod(token);
        std::getline(ss, token, ','); row.bids[i].price  = std::stod(token);
        std::getline(ss, token, ','); row.bids[i].amount = std::stod(token);
    }

    return row;
}