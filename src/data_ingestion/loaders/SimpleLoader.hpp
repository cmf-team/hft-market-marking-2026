#pragma once
#include <fstream>
#include <stdexcept>
#include <string>

namespace bt {

// Generic line-by-line file loader.
// Parser must expose: T parse_line(const std::string& line)
// Malformed lines (parse_line throws) are silently skipped.
template<typename Parser>
class SimpleLoader {
public:
    explicit SimpleLoader(const std::string& path, bool skip_header = false)
        : path_(path), skip_header_(skip_header) {}

    template<typename Consumer>
    void load(Consumer&& process) {
        std::ifstream file(path_);
        if (!file.is_open())
            throw std::runtime_error("Cannot open file: " + path_);

        std::string line;
        if (skip_header_) std::getline(file, line);

        while (std::getline(file, line)) {
            if (line.empty()) continue;
            try {
                process(parser_.parse_line(line));
            } catch (...) {}
        }
    }

private:
    std::string path_;
    Parser      parser_;
    bool        skip_header_;
};

} // namespace bt