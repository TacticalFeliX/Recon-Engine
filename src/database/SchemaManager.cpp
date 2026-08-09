#include "database/SchemaManager.hpp"
#include "database/DatabaseException.hpp"

#include <fstream>
#include <sstream>
#include <pqxx/pqxx>

namespace reconciliation::database {

namespace {
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("SchemaManager: could not open " + path);
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}
} // namespace

void SchemaManager::initializeSchema(DatabaseConnection& connection,
                                      const std::string& schemaPath,
                                      const std::string& indexesPath) {
    try {
        std::string schemaSql = readFile(schemaPath);
        pqxx::work schemaTxn(connection.handle());
        schemaTxn.exec(schemaSql);
        schemaTxn.commit();

        std::string indexesSql = readFile(indexesPath);
        pqxx::work indexTxn(connection.handle());
        indexTxn.exec(indexesSql);
        indexTxn.commit();
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("SchemaManager::initializeSchema failed: ") + e.what());
    }
}

} // namespace reconciliation::database
