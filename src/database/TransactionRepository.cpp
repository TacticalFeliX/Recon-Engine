#include "database/TransactionRepository.hpp"
#include "database/DatabaseException.hpp"

#include <pqxx/pqxx>

namespace reconciliation::database {

using models::Transaction;
using models::SourceSystem;
using models::Money;
using models::Date;

std::string TransactionRepository::tableFor(SourceSystem source) {
    // Table name comes from this fixed two-way switch, never from user
    // input, so string-concatenating it into SQL below is safe. Contrast
    // this with the $1, $2... bound parameters used for every actual data
    // value — those go through libpq's parameterized-query mechanism
    // specifically because they DO originate from file contents/user
    // input (see docs/DATABASE_DESIGN.md "Security" section, added later,
    // for the full SQL-injection discussion).
    return source == SourceSystem::Internal ? "internal_transactions" : "external_transactions";
}

TransactionRepository::TransactionRepository(DatabaseConnection& connection)
    : connection_(connection) {}

std::size_t TransactionRepository::insertBatch(SourceSystem source,
                                                const std::vector<Transaction>& transactions,
                                                int64_t batchId) {
    if (transactions.empty()) {
        return 0;
    }

    const std::string table = tableFor(source);
    const std::string sql =
        "INSERT INTO " + table +
        " (transaction_id, invoice_number, vendor_id, transaction_date, "
        "  amount_cents, tax_amount_cents, currency, ingestion_batch_id, source_row_number) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)";

    try {
        // Single transaction for the whole batch: see class-level comment
        // on atomicity. pqxx::work's destructor rolls back automatically
        // if commit() is never reached (e.g. an exception unwinds out of
        // this function) — that's RAII doing the safety work here, not
        // an explicit catch/rollback call.
        pqxx::work txn(connection_.handle());

        for (const Transaction& t : transactions) {
            txn.exec_params(sql,
                t.transactionId,
                t.invoiceNumber,
                t.vendorId,
                t.transactionDate.toString(),
                t.amount.cents(),
                t.taxAmount.cents(),
                t.currency,
                batchId,
                static_cast<int64_t>(t.sourceRowNumber)
            );
        }

        txn.commit();
        return transactions.size();
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("insertBatch failed (") + table + "): " + e.what());
    }
}

std::vector<Transaction> TransactionRepository::findAll(SourceSystem source) {
    const std::string table = tableFor(source);
    const std::string sql =
        "SELECT id, transaction_id, invoice_number, vendor_id, transaction_date, "
        "       amount_cents, tax_amount_cents, currency, source_row_number "
        "FROM " + table + " ORDER BY id";

    std::vector<Transaction> results;
    try {
        pqxx::work txn(connection_.handle());
        pqxx::result r = txn.exec(sql);
        txn.commit();

        results.reserve(r.size());
        for (const auto& row : r) {
            Transaction t;
            t.dbId = row[0].as<int64_t>();
            t.transactionId = row[1].as<std::string>();
            t.invoiceNumber = row[2].as<std::string>();
            t.vendorId = row[3].as<std::string>();
            t.transactionDate = Date::parse(row[4].as<std::string>());
            t.amount = Money(row[5].as<int64_t>());
            t.taxAmount = Money(row[6].as<int64_t>());
            t.currency = row[7].as<std::string>();
            t.source = source;
            t.sourceRowNumber = static_cast<std::size_t>(row[8].as<int64_t>());
            results.push_back(std::move(t));
        }
        return results;
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("findAll failed (") + table + "): " + e.what());
    }
}

std::size_t TransactionRepository::countAll(SourceSystem source) {
    const std::string table = tableFor(source);
    try {
        pqxx::work txn(connection_.handle());
        pqxx::result r = txn.exec("SELECT COUNT(*) FROM " + table);
        txn.commit();
        return static_cast<std::size_t>(r[0][0].as<int64_t>());
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("countAll failed (") + table + "): " + e.what());
    }
}

} // namespace reconciliation::database
