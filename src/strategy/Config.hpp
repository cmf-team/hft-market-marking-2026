#pragma once

#include <algorithm>
#include <charconv>
#include <fstream>
#include <string>
#include <unordered_map>

namespace cmf
{

class KvConfig
{
    std::unordered_map<std::string, std::string> data_;

    static std::string trim(std::string s)
    {
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

public:
    static KvConfig from_file(const std::string& path)
    {
        KvConfig cfg;
        if (path.empty())
            return cfg;
        std::ifstream f(path);
        if (!f)
            return cfg;
        std::string line;
        while (std::getline(f, line))
        {
            const auto hash = line.find('#');
            if (hash != std::string::npos)
                line.erase(hash);
            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            auto key = trim(line.substr(0, eq));
            auto val = trim(line.substr(eq + 1));
            if (!key.empty())
                cfg.data_[std::move(key)] = std::move(val);
        }
        return cfg;
    }

    void apply_cli_overrides(int argc, char** argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a.rfind("--", 0) != 0)
                continue;
            a.erase(0, 2);
            const auto eq = a.find('=');
            if (eq == std::string::npos)
                continue;
            data_[trim(a.substr(0, eq))] = trim(a.substr(eq + 1));
        }
    }

    double get_double(const std::string& key, double def) const
    {
        const auto it = data_.find(key);
        if (it == data_.end())
            return def;
        double v = def;
        std::from_chars(it->second.data(), it->second.data() + it->second.size(), v);
        return v;
    }

    int get_int(const std::string& key, int def) const
    {
        const auto it = data_.find(key);
        if (it == data_.end())
            return def;
        int v = def;
        std::from_chars(it->second.data(), it->second.data() + it->second.size(), v);
        return v;
    }

    std::string get_string(const std::string& key, const std::string& def = {}) const
    {
        const auto it = data_.find(key);
        return it == data_.end() ? def : it->second;
    }

    bool has(const std::string& key) const { return data_.count(key) > 0; }
};

} // namespace cmf
