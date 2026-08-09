#include "database/ConfigLoader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>
#include <cctype>

namespace reconciliation::database {

namespace {
std::string trim(const std::string& s) {
    auto start = std::find_if_not(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return (start < end) ? std::string(start, end) : std::string();
}
} // namespace

ConfigLoader ConfigLoader::loadFromFile(const std::string& path) {
    ConfigLoader loader;
    std::ifstream file(path);
    if (!file.is_open()) {
        // Not fatal: all required values might come from real environment
        // variables instead (e.g. in a CI pipeline with no .env file).
        return loader;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue; // silently skip malformed lines; this is a dev-convenience file, not user input requiring strict validation

        std::string key = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));
        if (!key.empty()) {
            loader.values_[key] = value;
        }
    }
    return loader;
}

std::string ConfigLoader::get(const std::string& key, const std::string& defaultValue) const {
    if (const char* envVal = std::getenv(key.c_str())) {
        return std::string(envVal);
    }
    auto it = values_.find(key);
    if (it != values_.end()) {
        return it->second;
    }
    return defaultValue;
}

std::string ConfigLoader::require(const std::string& key) const {
    if (const char* envVal = std::getenv(key.c_str())) {
        return std::string(envVal);
    }
    auto it = values_.find(key);
    if (it != values_.end()) {
        return it->second;
    }
    throw std::runtime_error("ConfigLoader::require: missing required config key '" + key + "'");
}

} // namespace reconciliation::database
