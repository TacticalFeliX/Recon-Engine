#pragma once

#include <string>
#include "database/DatabaseConnection.hpp"

namespace reconciliation::database {

// ---------------------------------------------------------------------------
// SchemaManager
//
// Reads sql/schema.sql and sql/indexes.sql from disk and executes them
// against the connected database, for `reconcile init-db`. Deliberately
// thin: this class does NOT duplicate the schema in C++ (e.g. as string
// literals or a migration DSL) — sql/*.sql remains the single source of
// truth for the schema, which is also what a developer runs directly via
// `psql -f sql/schema.sql` for manual inspection or CI setup. This class
// just automates reading and executing those same files.
// ---------------------------------------------------------------------------
class SchemaManager {
public:
    // Executes the full contents of schema.sql, then indexes.sql, against
    // the given connection. Both files are executed as multi-statement
    // batches (libpqxx's plain exec() supports semicolon-separated
    // statements in one call, unlike exec_params which is for a single
    // parameterized statement).
    static void initializeSchema(DatabaseConnection& connection,
                                  const std::string& schemaPath = "sql/schema.sql",
                                  const std::string& indexesPath = "sql/indexes.sql");
};

} // namespace reconciliation::database
