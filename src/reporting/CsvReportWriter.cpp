#include "reporting/CsvReportWriter.hpp"

#include <fstream>
#include <stdexcept>

namespace reconciliation::reporting {

namespace {
// Minimal CSV field escaping: if a field contains a comma, quote, or
// newline, wrap it in quotes and double any internal quotes. None of our
// current fields (transaction IDs, status names, decimal amounts)
// actually need this, but mismatch details are free text and could
// theoretically contain a comma — so this is applied defensively rather
// than assumed unnecessary.
std::string csvEscape(const std::string& field) {
    bool needsQuoting = field.find_first_of(",\"\n") != std::string::npos;
    if (!needsQuoting) return field;

    std::string escaped = "\"";
    for (char c : field) {
        if (c == '"') escaped += "\"\"";
        else escaped += c;
    }
    escaped += "\"";
    return escaped;
}
} // namespace

void CsvReportWriter::write(const std::string& path, const std::vector<engine::ReconciliationResult>& results) {
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("CsvReportWriter: could not open file for writing: " + path);
    }

    file << "transaction_id,status,internal_amount,external_amount,difference\n";

    for (const auto& r : results) {
        std::string internalAmount = r.internalTransaction ? r.internalTransaction->amount.toString() : "";
        std::string externalAmount = r.externalTransaction ? r.externalTransaction->amount.toString() : "";

        file << csvEscape(r.transactionId) << ","
             << engine::toString(r.status) << ","
             << internalAmount << ","
             << externalAmount << ","
             << r.amountDifferenceCents << "\n";
    }
}

} // namespace reconciliation::reporting
