#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>
#include <array>
#include <ostream>
#include <sstream>
#include <iomanip>

namespace reconciliation::models {

// ---------------------------------------------------------------------------
// Date
//
// WHY NOT std::string for dates?
//   If dates stay as strings, every comparison during reconciliation
//   ("is internal.date == external.date?") becomes a string comparison,
//   and any date-range or sorting logic needs to re-parse the string every
//   time. A small struct of three ints is 4-8 bytes, trivially comparable
//   with built-in operators, and lets us validate once at ingestion time
//   instead of re-validating (or silently failing) at every later use.
//
// WHY NOT std::chrono::year_month_day directly at the parsing boundary?
//   We do convert to it internally for validation (day-of-month bounds,
//   leap years), but we expose a minimal explicit struct as the model's
//   public type so the rest of the codebase (SQL layer, CSV writer) has a
//   simple, stable interface independent of <chrono>'s calendar API
//   surface. This is a deliberate encapsulation choice, not a rejection
//   of <chrono>.
// ---------------------------------------------------------------------------
struct Date {
    int16_t year = 0;
    int8_t month = 0;
    int8_t day = 0;

    Date() = default;
    Date(int16_t y, int8_t m, int8_t d) : year(y), month(m), day(d) {}

    static bool isLeapYear(int y) {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    }

    static int daysInMonth(int y, int m) {
        static constexpr std::array<int, 13> days =
            {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (m == 2 && isLeapYear(y)) return 29;
        return days[static_cast<size_t>(m)];
    }

    // Parses strict ISO-8601 "YYYY-MM-DD". Throws std::invalid_argument on
    // any malformed or out-of-range date (e.g. "2024-02-30"). Reconciliation
    // and validation both rely on this being strict — a silently "fixed"
    // date would corrupt audit trails (see §47 in the project spec).
    static Date parse(const std::string& s) {
        if (s.size() != 10 || s[4] != '-' || s[7] != '-') {
            throw std::invalid_argument("Date::parse: expected YYYY-MM-DD, got '" + s + "'");
        }
        for (size_t i : {0, 1, 2, 3, 5, 6, 8, 9}) {
            if (s[i] < '0' || s[i] > '9') {
                throw std::invalid_argument("Date::parse: non-numeric field in '" + s + "'");
            }
        }
        int y = std::stoi(s.substr(0, 4));
        int m = std::stoi(s.substr(5, 2));
        int d = std::stoi(s.substr(8, 2));

        if (m < 1 || m > 12) {
            throw std::invalid_argument("Date::parse: invalid month in '" + s + "'");
        }
        if (d < 1 || d > daysInMonth(y, m)) {
            throw std::invalid_argument("Date::parse: invalid day in '" + s + "'");
        }
        return Date(static_cast<int16_t>(y), static_cast<int8_t>(m), static_cast<int8_t>(d));
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(4) << static_cast<int>(year) << '-'
            << std::setw(2) << static_cast<int>(month) << '-'
            << std::setw(2) << static_cast<int>(day);
        return oss.str();
    }

    bool operator==(const Date& o) const {
        return year == o.year && month == o.month && day == o.day;
    }
    bool operator!=(const Date& o) const { return !(*this == o); }
    bool operator<(const Date& o) const {
        if (year != o.year) return year < o.year;
        if (month != o.month) return month < o.month;
        return day < o.day;
    }

    bool isValid() const { return year != 0; }
};

inline std::ostream& operator<<(std::ostream& os, const Date& d) {
    return os << d.toString();
}

} // namespace reconciliation::models
