#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace cmf {

class CsvReader {
public:
    explicit CsvReader(const std::string& filename, char delimiter = ',')
        : file_(filename), delimiter_(delimiter) {
        if (!file_.is_open())
            throw std::runtime_error("Cannot open " + filename);
    }

    bool readRow(std::vector<std::string>& fields) {
        std::string line;
        if (!std::getline(file_, line)) return false;
        std::stringstream ss(line);
        std::string field;
        fields.clear();
        while (std::getline(ss, field, delimiter_)) {
            fields.push_back(field);
        }
        return true;
    }
private:
    std::ifstream file_;
    char delimiter_;
};

} // namespace cmf