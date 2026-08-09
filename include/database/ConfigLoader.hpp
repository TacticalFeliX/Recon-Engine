#pragma once

#include <string>
#include <unordered_map>
#include <optional>

namespace reconciliation::database {

// ---------------------------------------------------------------------------
// ConfigLoader
//
// WHY hand-rolled instead of a dotenv library?
//   The format we need to support is trivial (KEY=VALUE lines, '#'
//   comments, blank lines) and env-var override is a handful of lines
//   using std::getenv. Adding a dependency for this — on Windows,
//   another vcpkg package to install — isn't justified (same reasoning
//   as Logger; see spec §21/§43 "avoid unnecessary dependencies").
//
// PRECEDENCE: real environment variables always win over .env file values.
// This matches common convention (12-factor apps) and lets CI/deployment
// environments override local .env files without editing them.
// ---------------------------------------------------------------------------
class ConfigLoader {
public:
    // Loads KEY=VALUE pairs from the given file path. Missing file is not
    // an error — .env is optional if all values are supplied via real
    // environment variables (e.g. in CI).
    static ConfigLoader loadFromFile(const std::string& path);

    // Looks up a key: real environment variable first, then the loaded
    // .env file, then the provided default.
    std::string get(const std::string& key, const std::string& defaultValue = "") const;

    // Same lookup, but throws std::runtime_error if the key is missing
    // everywhere. Used for values that have no safe default (e.g. DB
    // password) — silently proceeding with an empty password would be a
    // security footgun, not a convenience.
    std::string require(const std::string& key) const;

private:
    std::unordered_map<std::string, std::string> values_;
};

} // namespace reconciliation::database
