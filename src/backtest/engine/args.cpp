#include "args.hpp"

#include <format>
#include <iostream>

void print_usage(std::string_view prog)
{
    std::string strategies;
    for (const auto st : ALL_STRATEGIES)
    {
        if (!strategies.empty())
            strategies += '|';
        strategies += strategy_name(st);
    }
    std::cout << std::format(
        "Usage: {} [options]\n"
        "  --lob                  <path>   Path to lob.csv              (required)\n"
        "  --trades               <path>   Path to trades.csv           (required)\n"
        "  --strategy             <name>   Strategy: {}  (repeatable, default: replay)\n"
        "  --target-qty           <n>      Target quantity (passive, trend) (default: 1000)\n"
        "  --maker-fee-bps        <bps>    Maker fee in basis points    (default: 0.0)\n"
        "  --pnl-sample-interval  <n>      PnL sample rate (events)     (default: 1000)\n"
        "  --help                          Show this message\n",
        prog, strategies);
}

Config parse_args(std::span<char*> args)
{
    Config cfg;
    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view key = args[i];
        auto need_val = [&]() -> std::string_view
        {
            if (i + 1 >= args.size())
            {
                std::cerr << std::format("Error: {} requires a value\n", key);
                std::exit(1);
            }
            return args[++i];
        };

        if (key == "--lob")
            cfg.lob_path = std::string(need_val());
        else if (key == "--trades")
            cfg.trades_path = std::string(need_val());
        else if (key == "--strategy")
        {
            std::string_view val = need_val();
            if (val == "*")
            {
                for (const auto& st : ALL_STRATEGIES)
                {
                    cfg.strategy_types.push_back(st);
                }
            }
            else
            {
                bool found = false;
                for (const auto st : ALL_STRATEGIES)
                {
                    if (val == strategy_name(st))
                    {
                        cfg.strategy_types.push_back(st);
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    std::string valid;
                    for (const auto st : ALL_STRATEGIES)
                    {
                        if (!valid.empty())
                            valid += '|';
                        valid += strategy_name(st);
                    }
                    std::cerr << std::format("Error: unknown strategy '{}' (use {})\n", val, valid);
                    std::exit(1);
                }
            }
        }
        else if (key == "--target-qty")
        {
            std::string_view val = need_val();
            parse_numeric(val, cfg.target_qty, "--target-qty");
        }
        else if (key == "--maker-fee-bps")
        {
            std::string_view val = need_val();
            parse_numeric(val, cfg.maker_fee_bps, "--maker-fee-bps");
        }
        else if (key == "--pnl-sample-interval")
        {
            std::string_view val = need_val();
            parse_numeric(val, cfg.pnl_sample_interval, "--pnl-sample-interval");
        }
        else if (key == "--help")
        {
            print_usage(args[0]);
            std::exit(0);
        }
        else
        {
            std::cerr << std::format("Error: unknown option '{}'\n", key);
            print_usage(args[0]);
            std::exit(1);
        }
    }

    if (cfg.lob_path.empty() || cfg.trades_path.empty())
    {
        std::cerr << "Error: --lob and --trades are required\n";
        print_usage(args[0]);
        std::exit(1);
    }

    if (cfg.strategy_types.empty())
    {
        cfg.strategy_types.push_back(StrategyType::Replay);
    }

    return cfg;
}
