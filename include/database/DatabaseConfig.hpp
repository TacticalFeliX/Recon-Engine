#pragma once

#include <string>
#include "database/ConfigLoader.hpp"

namespace reconciliation::database {

// ---------------------------------------------------------------------------
// DatabaseConfig
// Typed connection parameters, built from ConfigLoader so nothing in the
// rest of the codebase deals with raw string key lookups. connectionString()
// produces a libpq keyword/value connection string, e.g.:
//   "host=localhost port=5432 dbname=reconciliation user=recon_user password=..."
// ---------------------------------------------------------------------------
struct DatabaseConfig {
    std::string host;
    std::string port;
    std::string dbName;
    std::string user;
    std::string password;

    static DatabaseConfig fromConfigLoader(const ConfigLoader& config) {
        DatabaseConfig dbConfig;
        dbConfig.host = config.get("RECON_DB_HOST", "localhost");
        dbConfig.port = config.get("RECON_DB_PORT", "5432");
        dbConfig.dbName = config.get("RECON_DB_NAME", "reconciliation");
        dbConfig.user = config.get("RECON_DB_USER", "recon_user");
        // Password has no safe default — require() throws a clear error
        // rather than silently trying to connect with an empty password.
        dbConfig.password = config.require("RECON_DB_PASSWORD");
        return dbConfig;
    }

    std::string connectionString() const {
        return "host=" + host +
               " port=" + port +
               " dbname=" + dbName +
               " user=" + user +
               " password=" + password;
    }
};

} // namespace reconciliation::database
