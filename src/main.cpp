// Backtester CLI entry point.
//
// Usage:
//   backtester --lob <lob.csv> --trades <trades.csv> --out <out_dir>
//              [--config <config_path>]
//              [--tick-size <double>] [--qty-scale <double>]
//              [--submit-us <int>] [--cancel-us <int>] [--fill-us <int>]
//              [--strategy static|as]
//              [--quote-size <int>]
//              [--gamma <double>] [--k <double>] [--sigma-init <double>]
//              [--vol-alpha <double>] [--horizon-us <int>]
//
// Config file (optional, key=value, '#' starts a comment) can supply any of
// the same knobs; CLI flags override config-file values. Defaults match the
// user's sample data: tick_size=1e-7, qty_scale=1, all latencies=0,
// quote_size=1.
//
// Outputs (written to <out_dir>):
//   report.txt    — human-readable summary (counters, PnL, drawdown, spec)
//   equity.csv    — equity curve sample per book event

#include "engine/backtest_engine.hpp"

#include "bt/avellaneda_stoikov_quoter.hpp"
#include "bt/csv_lob_loader.hpp"
#include "bt/csv_trade_loader.hpp"
#include "bt/event_stream.hpp"
#include "bt/latency_model.hpp"
#include "bt/micro_price_quoter.hpp"
#include "bt/queue_model.hpp"
#include "bt/static_quoter.hpp"
#include "bt/stats.hpp"
#include "bt/strategy.hpp"
#include "bt/types.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

struct Config {
    std::string lob_path;
    std::string trades_path;
    std::string out_dir;
    std::string config_path;

    double           tick_size      = 1e-7;
    double           qty_scale      = 1.0;
    bt::Timestamp    submit_us      = 0;
    bt::Timestamp    cancel_us      = 0;
    bt::Timestamp    fill_us        = 0;
    bt::Timestamp    sample_us      = 1'000'000;  // equity-curve sample interval; 1s default

    // Strategy selection: "static" or "as" (Avellaneda-Stoikov).
    std::string      strategy       = "as";
    bt::Qty          quote_size     = 1;

    // Avellaneda-Stoikov parameters (ignored when strategy != "as").
    double           gamma          = 0.1;
    double           k              = 1.5;
    double           sigma_init     = 1.0;
    double           vol_alpha      = 0.05;
    bt::Timestamp    horizon_us     = 86'400'000'000LL;  // 1 day

    // Micro-price parameters (ignored when strategy != "micro").
    bt::Price        mp_half_spread     = 1;     // ticks
    double           mp_inventory_skew  = 0.0;   // ticks per inventory unit
    std::size_t      mp_imbalance_depth = 1;     // levels averaged into I
    bool             mp_passive_only    = true;  // clamp quotes to same-side touch
};

void print_usage() {
    std::cerr <<
        "Usage: backtester --lob <lob.csv> --trades <trades.csv> --out <out_dir>\n"
        "                  [--config <config_path>]\n"
        "                  [--tick-size <double>] [--qty-scale <double>]\n"
        "                  [--submit-us <int>] [--cancel-us <int>] [--fill-us <int>]\n"
        "                  [--sample-us <int>]\n"
        "                  [--strategy static|as|micro] [--quote-size <int>]\n"
        "                  [--gamma <double>] [--k <double>] [--sigma-init <double>]\n"
        "                  [--vol-alpha <double>] [--horizon-us <int>]\n"
        "                  [--mp-half-spread <int>] [--mp-skew <double>]\n"
        "                  [--mp-imbalance-depth <int>] [--mp-passive-only 0|1]\n";
}

[[nodiscard]] std::string trim(std::string_view sv) {
    const auto b = sv.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    const auto e = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(b, e - b + 1));
}

void apply_kv(Config& cfg, const std::string& key, const std::string& val) {
    if      (key == "lob")         cfg.lob_path    = val;
    else if (key == "trades")      cfg.trades_path = val;
    else if (key == "out")         cfg.out_dir     = val;
    else if (key == "tick_size")   cfg.tick_size   = std::stod(val);
    else if (key == "qty_scale")   cfg.qty_scale   = std::stod(val);
    else if (key == "submit_us")   cfg.submit_us   = std::stoll(val);
    else if (key == "cancel_us")   cfg.cancel_us   = std::stoll(val);
    else if (key == "fill_us")     cfg.fill_us     = std::stoll(val);
    else if (key == "sample_us")   cfg.sample_us   = std::stoll(val);
    else if (key == "strategy")    cfg.strategy    = val;
    else if (key == "quote_size")  cfg.quote_size  = std::stoll(val);
    else if (key == "gamma")       cfg.gamma       = std::stod(val);
    else if (key == "k")           cfg.k           = std::stod(val);
    else if (key == "sigma_init")  cfg.sigma_init  = std::stod(val);
    else if (key == "vol_alpha")   cfg.vol_alpha   = std::stod(val);
    else if (key == "horizon_us")  cfg.horizon_us  = std::stoll(val);
    else if (key == "mp_half_spread")     cfg.mp_half_spread     = std::stoll(val);
    else if (key == "mp_inventory_skew")  cfg.mp_inventory_skew  = std::stod(val);
    else if (key == "mp_imbalance_depth") cfg.mp_imbalance_depth = static_cast<std::size_t>(std::stoull(val));
    else if (key == "mp_passive_only")    cfg.mp_passive_only    = (std::stoi(val) != 0);
    else throw std::runtime_error("unknown config key: " + key);
}

void load_config_file(Config& cfg, const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open config file: " + path);
    std::string line;
    int lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        // strip comment
        if (const auto h = line.find('#'); h != std::string::npos) line.erase(h);
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            if (!trim(line).empty()) {
                throw std::runtime_error(path + ":" + std::to_string(lineno) +
                                         ": expected key=value");
            }
            continue;
        }
        const auto key = trim(std::string_view(line).substr(0, eq));
        const auto val = trim(std::string_view(line).substr(eq + 1));
        if (key.empty()) continue;
        apply_kv(cfg, key, val);
    }
}

Config parse_args(int argc, char** argv) {
    Config cfg;

    auto need_value = [&](int& i, const char* flag) -> std::string {
        if (i + 1 >= argc) throw std::runtime_error(std::string(flag) + " requires a value");
        return argv[++i];
    };

    // Two passes: first read --config (if any) so file values land before
    // CLI flags overlay them.
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--config") cfg.config_path = need_value(i, "--config");
    }
    if (!cfg.config_path.empty()) load_config_file(cfg, cfg.config_path);

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if      (a == "--lob")         cfg.lob_path    = need_value(i, a.c_str());
        else if (a == "--trades")      cfg.trades_path = need_value(i, a.c_str());
        else if (a == "--out")         cfg.out_dir     = need_value(i, a.c_str());
        else if (a == "--config")      { (void)need_value(i, a.c_str()); /* already handled */ }
        else if (a == "--tick-size")   cfg.tick_size   = std::stod(need_value(i, a.c_str()));
        else if (a == "--qty-scale")   cfg.qty_scale   = std::stod(need_value(i, a.c_str()));
        else if (a == "--submit-us")   cfg.submit_us   = std::stoll(need_value(i, a.c_str()));
        else if (a == "--cancel-us")   cfg.cancel_us   = std::stoll(need_value(i, a.c_str()));
        else if (a == "--fill-us")     cfg.fill_us     = std::stoll(need_value(i, a.c_str()));
        else if (a == "--sample-us")   cfg.sample_us   = std::stoll(need_value(i, a.c_str()));
        else if (a == "--strategy")    cfg.strategy    = need_value(i, a.c_str());
        else if (a == "--quote-size")  cfg.quote_size  = std::stoll(need_value(i, a.c_str()));
        else if (a == "--gamma")       cfg.gamma       = std::stod(need_value(i, a.c_str()));
        else if (a == "--k")           cfg.k           = std::stod(need_value(i, a.c_str()));
        else if (a == "--sigma-init")  cfg.sigma_init  = std::stod(need_value(i, a.c_str()));
        else if (a == "--vol-alpha")   cfg.vol_alpha   = std::stod(need_value(i, a.c_str()));
        else if (a == "--horizon-us")  cfg.horizon_us  = std::stoll(need_value(i, a.c_str()));
        else if (a == "--mp-half-spread")     cfg.mp_half_spread     = std::stoll(need_value(i, a.c_str()));
        else if (a == "--mp-skew")            cfg.mp_inventory_skew  = std::stod(need_value(i, a.c_str()));
        else if (a == "--mp-imbalance-depth") cfg.mp_imbalance_depth = static_cast<std::size_t>(std::stoull(need_value(i, a.c_str())));
        else if (a == "--mp-passive-only")    cfg.mp_passive_only    = (std::stoi(need_value(i, a.c_str())) != 0);
        else if (a == "-h" || a == "--help") { print_usage(); std::exit(0); }
        else throw std::runtime_error("unknown argument: " + a);
    }

    if (cfg.lob_path.empty() || cfg.trades_path.empty() || cfg.out_dir.empty()) {
        print_usage();
        throw std::runtime_error("--lob, --trades, and --out are required");
    }
    return cfg;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Config cfg = parse_args(argc, argv);

        const bt::InstrumentSpec spec{ cfg.tick_size, cfg.qty_scale };
        std::error_code ec;
        fs::create_directories(cfg.out_dir, ec);
        if (ec) throw std::runtime_error("cannot create out dir " + cfg.out_dir +
                                         ": " + ec.message());

        bt::CsvLobLoader      lob(cfg.lob_path,    spec);
        bt::CsvTradeLoader    trd(cfg.trades_path, spec);
        bt::MergedEventStream stream(lob, trd);

        bt::PessimisticQueueModel qm;
        bt::FixedLatencyModel     lm(cfg.submit_us, cfg.cancel_us, cfg.fill_us);

        std::unique_ptr<bt::IStrategy> strat;
        if (cfg.strategy == "static") {
            strat = std::make_unique<bt::StaticQuoter>(cfg.quote_size);
        } else if (cfg.strategy == "as" || cfg.strategy == "avellaneda_stoikov") {
            bt::AvellanedaStoikovQuoter::Params p;
            p.quote_size     = cfg.quote_size;
            p.gamma          = cfg.gamma;
            p.k              = cfg.k;
            p.sigma_init     = cfg.sigma_init;
            p.vol_ewma_alpha = cfg.vol_alpha;
            p.horizon_us     = cfg.horizon_us;
            strat = std::make_unique<bt::AvellanedaStoikovQuoter>(p);
        } else if (cfg.strategy == "micro" || cfg.strategy == "micro_price") {
            bt::MicroPriceQuoter::Params p;
            p.quote_size       = cfg.quote_size;
            p.half_spread      = cfg.mp_half_spread;
            p.inventory_skew   = cfg.mp_inventory_skew;
            p.imbalance_depth  = cfg.mp_imbalance_depth;
            p.passive_only     = cfg.mp_passive_only;
            strat = std::make_unique<bt::MicroPriceQuoter>(p);
        } else {
            throw std::runtime_error("unknown --strategy: " + cfg.strategy +
                                     " (expected 'static', 'as', or 'micro')");
        }

        bt::BacktestEngine engine(stream, qm, lm, *strat);
        engine.set_sample_interval(cfg.sample_us);

        std::cout << "Running backtest:\n"
                  << "  lob        = " << cfg.lob_path    << '\n'
                  << "  trades     = " << cfg.trades_path << '\n'
                  << "  out        = " << cfg.out_dir     << '\n'
                  << "  tick_size  = " << cfg.tick_size   << '\n'
                  << "  latencies  = submit/cancel/fill us = "
                  << cfg.submit_us << '/' << cfg.cancel_us << '/' << cfg.fill_us << '\n'
                  << "  sample_us  = " << cfg.sample_us   << '\n'
                  << "  strategy   = " << cfg.strategy    << '\n'
                  << "  quote_size = " << cfg.quote_size  << '\n';
        if (cfg.strategy == "as" || cfg.strategy == "avellaneda_stoikov") {
            std::cout << "  gamma      = " << cfg.gamma      << '\n'
                      << "  k          = " << cfg.k          << '\n'
                      << "  sigma_init = " << cfg.sigma_init << '\n'
                      << "  vol_alpha  = " << cfg.vol_alpha  << '\n'
                      << "  horizon_us = " << cfg.horizon_us << '\n';
        } else if (cfg.strategy == "micro" || cfg.strategy == "micro_price") {
            std::cout << "  mp_half_spread     = " << cfg.mp_half_spread     << '\n'
                      << "  mp_inventory_skew  = " << cfg.mp_inventory_skew  << '\n'
                      << "  mp_imbalance_depth = " << cfg.mp_imbalance_depth << '\n'
                      << "  mp_passive_only    = " << (cfg.mp_passive_only ? "true" : "false") << '\n';
        }
        std::cout << std::flush;

        const auto t0 = std::chrono::steady_clock::now();
        const auto event_count = engine.run();
        const auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(t1 - t0).count();

        const auto& s = engine.stats();
        std::cout << "\nProcessed " << event_count << " events in "
                  << secs << " s ("
                  << (secs > 0.0 ? static_cast<double>(event_count) / secs : 0.0)
                  << " events/sec)\n";
        std::cout << "Submitted=" << s.submitted_count()
                  << " Rejected=" << s.rejected_count()
                  << " Fills="    << s.fill_count()
                  << " Volume="   << s.total_volume() << '\n';

        const fs::path summary_path = fs::path(cfg.out_dir) / "report.txt";
        const fs::path equity_path  = fs::path(cfg.out_dir) / "equity.csv";
        const fs::path fills_path   = fs::path(cfg.out_dir) / "fills.csv";
        {
            std::ofstream out(summary_path);
            if (!out) throw std::runtime_error("cannot write " + summary_path.string());
            s.write_summary(out, spec, engine.portfolio().avg_entry_price());
        }
        {
            std::ofstream out(equity_path);
            if (!out) throw std::runtime_error("cannot write " + equity_path.string());
            s.write_equity_csv(out, spec);
        }
        {
            std::ofstream out(fills_path);
            if (!out) throw std::runtime_error("cannot write " + fills_path.string());
            s.write_fills_csv(out, spec);
        }
        std::cout << "\nWrote " << summary_path.string()
                  << "\n      " << equity_path.string()
                  << "\n      " << fills_path.string()  << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
