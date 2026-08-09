#pragma once

#include <memory>
#include <string>
#include <pqxx/pqxx>
#include "database/DatabaseConfig.hpp"

namespace reconciliation::database {

// ---------------------------------------------------------------------------
// DatabaseConnection
//
// WHY a wrapper class instead of using pqxx::connection directly everywhere?
//   1. RAII ownership in one place: the connection opens in the
//      constructor and closes automatically when this object goes out of
//      scope (pqxx::connection's own destructor handles the actual
//      socket teardown — we're not reimplementing that, just centralizing
//      *where* a connection gets created so the rest of the app doesn't
//      each hold their own raw connection string logic).
//   2. Connection errors are translated into our own ConnectionException
//      at construction time, so callers (CLI, repositories) catch one
//      exception type instead of needing to know pqxx::broken_connection
//      specifically.
//   3. It's the natural place to add connection pooling later if the
//      project grows to need concurrent workers (see
//      docs/FUTURE_IMPROVEMENTS.md) without changing every repository's
//      interface.
//
// NON-COPYABLE: a database connection is a unique resource (one TCP
// socket, one backend process on the server side). Copying it would
// either be a shallow copy (two objects both thinking they own — and
// will close — the same socket) or require duplicating the whole
// connection, which pqxx doesn't support implicitly. Deleting the copy
// constructor/assignment makes accidental copies a compile error instead
// of a runtime bug.
// ---------------------------------------------------------------------------
class DatabaseConnection {
public:
    explicit DatabaseConnection(const DatabaseConfig& config);

    DatabaseConnection(const DatabaseConnection&) = delete;
    DatabaseConnection& operator=(const DatabaseConnection&) = delete;
    DatabaseConnection(DatabaseConnection&&) = default;
    DatabaseConnection& operator=(DatabaseConnection&&) = default;

    // Exposes the underlying pqxx::connection for repositories to build
    // pqxx::work (transaction) objects from. Repositories, not this class,
    // own SQL statement text — this class's only job is connection
    // lifecycle (see spec §9: "don't scatter SQL queries throughout the
    // application" — the flip side is this class shouldn't contain any).
    pqxx::connection& handle();

    bool isOpen() const;

private:
    std::unique_ptr<pqxx::connection> connection_;
};

} // namespace reconciliation::database
