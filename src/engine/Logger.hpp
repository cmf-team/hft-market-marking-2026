#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace cmf
{

class Logger
{
public:
    static void set_path(const std::string& p)
    {
        std::lock_guard<std::mutex> g(mu());
        if (ofs().is_open())
            ofs().close();
        ofs().open(p, std::ios::out | std::ios::app);
    }

    static void log(const std::string& line)
    {
        std::lock_guard<std::mutex> g(mu());
        if (!ofs().is_open())
            return;
        ofs() << line << '\n';
        ofs().flush();
    }

private:
    static std::ofstream& ofs()
    {
        static std::ofstream f;
        return f;
    }
    static std::mutex& mu()
    {
        static std::mutex m;
        return m;
    }
};

} // namespace cmf
