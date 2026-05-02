#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>


// ,local_timestamp,side,price,amount
// 0,1722470400014926,sell,0.0110435,734

struct MarketTradeRow {
    long long index;
    long long local_timestamp;
    std::string side;
    double price;
    double amount;
};

MarketTradeRow parseMarketTrade(const std::string& line) {
    MarketTradeRow row;

    std::stringstream ss(line);
    std::string token;
    std::getline(ss, token, ',');
    row.index = std::stoll(token);

    std::getline(ss, token, ',');
    row.local_timestamp = std::stoll(token);

    std::getline(ss, token, ',');
    row.side = token;

    std::getline(ss, token, ',');
    row.price = std::stod(token);

    std::getline(ss, token, ',');
    row.amount = std::stod(token);

    return row;
}