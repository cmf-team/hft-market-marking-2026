#pragma once
#include <fstream>
#include <string>

struct Config {
    std::string lob_file      = "/project/data/lob.csv";
    std::string trades_file   = "/project/data/trades.csv";
    std::string output_csv    = "/project/data/results.csv";
    std::string output_report = "/project/data/report.txt";

    double gamma         = 0.1;
    double sigma         = 0.001;
    double k             = 10000.0;
    double T             = 1.0;
    double order_size    = 100.0;
    bool   use_microprice = true;

    static Config fromFile(const std::string& path) {
        Config cfg;
        std::ifstream f(path);
        if (!f.is_open()) return cfg;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            auto key = trim(line.substr(0, eq));
            auto val = trim(line.substr(eq + 1));
            if      (key == "lob_file")       cfg.lob_file       = val;
            else if (key == "trades_file")    cfg.trades_file    = val;
            else if (key == "output_csv")     cfg.output_csv     = val;
            else if (key == "output_report")  cfg.output_report  = val;
            else if (key == "gamma")          cfg.gamma          = std::stod(val);
            else if (key == "sigma")          cfg.sigma          = std::stod(val);
            else if (key == "k")              cfg.k              = std::stod(val);
            else if (key == "T")              cfg.T              = std::stod(val);
            else if (key == "order_size")     cfg.order_size     = std::stod(val);
            else if (key == "use_microprice") cfg.use_microprice = (val == "1" || val == "true");
        }
        return cfg;
    }

private:
    static std::string trim(const std::string& s) {
        auto a = s.find_first_not_of(" \t\r");
        auto b = s.find_last_not_of(" \t\r");
        return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
    }
};
