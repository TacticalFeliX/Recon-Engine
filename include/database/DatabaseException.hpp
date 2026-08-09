#pragma once

#include <stdexcept>
#include <string>

namespace reconciliation::database {

// ---------------------------------------------------------------------------
// DatabaseException
//
// WHY wrap pqxx's exceptions instead of letting them propagate directly?
//   pqxx throws several distinct exception types (pqxx::sql_error,
//   pqxx::broken_connection, pqxx::unique_violation, etc.), each with its
//   own constructor shape. Repository code catches those pqxx-specific
//   exceptions at the boundary and rethrows this single, project-specific
//   type with added context (which operation failed, on which table).
//   This keeps every layer above the database layer (ingestion,
//   reconciliation, CLI) able to catch ONE exception type for "something
//   went wrong talking to the database", rather than needing to know
//   about pqxx's exception hierarchy — this is the encapsulation benefit
//   discussed in spec §9 ("clean database layer... don't scatter SQL").
// ---------------------------------------------------------------------------
class DatabaseException : public std::runtime_error {
public:
    explicit DatabaseException(const std::string& message)
        : std::runtime_error(message) {}
};

class ConnectionException : public DatabaseException {
public:
    explicit ConnectionException(const std::string& message)
        : DatabaseException("Database connection error: " + message) {}
};

class DuplicateIngestionException : public DatabaseException {
public:
    explicit DuplicateIngestionException(const std::string& fileHash)
        : DatabaseException("File with hash " + fileHash + " has already been ingested (idempotency check)") {}
};

} // namespace reconciliation::database
