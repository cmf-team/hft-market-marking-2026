#include "report.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace hft {

std::string build_report_markdown(const BacktestConfig& config,
                                  const BacktestStats& stats,
                                  const std::string& strategy_name) {

    std::ostringstream out;
    out << std::fixed << std::setprecision(8);

    out << "# Backtest Performance Report\n\n";
    out << "## Run Setup\n";
    out << "- strategy: `" << strategy_name << "`\n";
    out << "- strategy_config: `" << config.strategy << "`\n";
    out << "- lob_path: `" << config.lob_path << "`\n";
    out << "- trades_path: `" << config.trades_path << "`\n";
    out << "- first_event_ts: `" << stats.first_event_ts << "`\n";
    out << "- last_event_ts: `" << stats.last_event_ts << "`\n";
    out << "- events_processed: `" << stats.total_events << "`\n";
    out << "- replay_speed: `" << config.replay_speed << "`\n";
    out << "- fill_mode: `market-price crosses order level`\n\n";

    out << "## PnL\n";
    out << "- initial_cash: `" << stats.initial_cash << "`\n";
    out << "- final_equity: `" << stats.final_equity << "`\n";
    out << "- total_pnl: `" << stats.total_pnl << "`\n";
    out << "- max_drawdown: `" << stats.max_drawdown << "`\n";
    out << "- ending_position: `" << stats.position << "`\n";
    out << "- inventory_peak_abs: `" << stats.inventory_peak_abs << "`\n\n";

    out << "## Execution Statistics\n";
    out << "- submitted_orders: `" << stats.submitted_orders << "`\n";
    out << "- cancelled_orders: `" << stats.cancelled_orders << "`\n";
    out << "- filled_orders: `" << stats.filled_orders << "`\n";
    out << "- fill_rate: `" << stats.fill_rate() << "`\n";
    out << "- maker_fills: `" << stats.maker_fills << "`\n";
    out << "- taker_fills: `" << stats.taker_fills << "`\n";
    out << "- buy_qty: `" << stats.buy_qty << "`\n";
    out << "- sell_qty: `" << stats.sell_qty << "`\n";
    out << "- avg_buy_price: `" << stats.avg_buy_price() << "`\n";
    out << "- avg_sell_price: `" << stats.avg_sell_price() << "`\n";
    out << "- turnover: `" << stats.turnover() << "`\n\n";

    out << "## Custom Features\n";
    out << "- avg_spread: `" << stats.avg_spread() << "`\n";

    for (const auto& [key, value] : stats.custom_features) {
        out << "- " << key << ": `" << value << "`\n";
    }

    return out.str();
}

bool write_report(const std::string& output_path, const std::string& content) {
    std::ofstream out(output_path);
    if (!out.is_open()) {
        return false;
    }

    out << content;
    return true;
}

}
