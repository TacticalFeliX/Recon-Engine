#include "ingestion/CsvParser.hpp"

namespace reconciliation::ingestion {

std::vector<std::string> CsvParser::splitLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == ',') {
            fields.push_back(current);
            current.clear();
        } else if (c != '\r') { // strip trailing CR from Windows-style line endings
            current.push_back(c);
        }
    }
    fields.push_back(current);
    return fields;
}

} // namespace reconciliation::ingestion
