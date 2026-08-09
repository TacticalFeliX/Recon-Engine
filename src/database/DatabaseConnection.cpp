#include "database/DatabaseConnection.hpp"
#include "database/DatabaseException.hpp"

namespace reconciliation::database {

DatabaseConnection::DatabaseConnection(const DatabaseConfig& config) {
    try {
        connection_ = std::make_unique<pqxx::connection>(config.connectionString());
    } catch (const std::exception& e) {
        // pqxx throws std::runtime_error-derived types (often
        // pqxx::broken_connection) with messages that already include
        // useful detail (host/port/reason). We wrap rather than discard
        // that detail.
        throw ConnectionException(e.what());
    }
}

pqxx::connection& DatabaseConnection::handle() {
    if (!connection_) {
        throw ConnectionException("handle() called on a moved-from DatabaseConnection");
    }
    return *connection_;
}

bool DatabaseConnection::isOpen() const {
    return connection_ && connection_->is_open();
}

} // namespace reconciliation::database
