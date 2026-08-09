#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>
#include <ostream>

namespace reconciliation::models {

// ---------------------------------------------------------------------------
// Money
//
// WHY NOT float/double?
//   Floating point numbers cannot represent most decimal fractions exactly
//   (0.1 in binary floating point is actually ~0.1000000000000000055...).
//   For a system whose entire purpose is detecting cent-level mismatches
//   between two data sources, that representation error is unacceptable —
//   it could either mask a real mismatch or invent a fake one.
//
// WHY int64_t cents?
//   Integers have exact arithmetic. Storing "amountInCents" means $125.50
//   is stored as 12550. Addition, subtraction, and equality comparisons
//   are then exact, which is required for financial correctness (see
//   docs/ARCHITECTURE.md, "Financial Correctness" section, added in a
//   later module).
//
//   int64_t gives headroom up to ~92 quadrillion cents (~$920 trillion),
//   far beyond any realistic single transaction, while still being a
//   cheap, cache-friendly 8-byte value (as opposed to an arbitrary
//   precision decimal type, which would be overkill here and slower to
//   compare/hash at reconciliation time).
// ---------------------------------------------------------------------------
class Money {
public:
    Money() : cents_(0) {}
    explicit Money(int64_t cents) : cents_(cents) {}

    // Parses a decimal string like "12500.50" into exact cents (1250050).
    // Throws std::invalid_argument on malformed input rather than silently
    // truncating or producing a garbage value — financial ingestion code
    // must never "guess" at a monetary value.
    static Money fromDecimalString(const std::string& s) {
        if (s.empty()) {
            throw std::invalid_argument("Money::fromDecimalString: empty string");
        }

        bool negative = false;
        size_t i = 0;
        if (s[0] == '-') { negative = true; i = 1; }
        else if (s[0] == '+') { i = 1; }

        int64_t wholePart = 0;
        int64_t fracPart = 0;
        bool sawDigit = false;
        bool sawDot = false;
        int fracDigits = 0;

        for (; i < s.size(); ++i) {
            char c = s[i];
            if (c == '.') {
                if (sawDot) {
                    throw std::invalid_argument("Money::fromDecimalString: multiple decimal points in '" + s + "'");
                }
                sawDot = true;
                continue;
            }
            if (c < '0' || c > '9') {
                throw std::invalid_argument("Money::fromDecimalString: invalid character in '" + s + "'");
            }
            sawDigit = true;
            int digit = c - '0';
            if (!sawDot) {
                wholePart = wholePart * 10 + digit;
            } else {
                if (fracDigits < 2) {
                    fracPart = fracPart * 10 + digit;
                    fracDigits++;
                }
                // Digits beyond the 2nd decimal place are intentionally
                // dropped rather than rounded, since sub-cent amounts are
                // not valid currency values in this system. A stricter
                // policy (reject instead of truncate) is a reasonable
                // alternative — see docs/RECONCILIATION_ALGORITHMS.md.
            }
        }

        if (!sawDigit) {
            throw std::invalid_argument("Money::fromDecimalString: no digits in '" + s + "'");
        }

        // Pad fractional part to 2 digits (e.g. "12.5" -> 50 cents, not 5).
        while (fracDigits < 2) {
            fracPart *= 10;
            fracDigits++;
        }

        int64_t total = wholePart * 100 + fracPart;
        return Money(negative ? -total : total);
    }

    int64_t cents() const { return cents_; }

    double toDouble() const { return static_cast<double>(cents_) / 100.0; }

    std::string toString() const {
        int64_t c = cents_;
        bool neg = c < 0;
        if (neg) c = -c;
        int64_t whole = c / 100;
        int64_t frac = c % 100;
        std::string result = (neg ? "-" : "") + std::to_string(whole) + ".";
        if (frac < 10) result += "0";
        result += std::to_string(frac);
        return result;
    }

    Money operator+(const Money& other) const { return Money(cents_ + other.cents_); }
    Money operator-(const Money& other) const { return Money(cents_ - other.cents_); }
    bool operator==(const Money& other) const { return cents_ == other.cents_; }
    bool operator!=(const Money& other) const { return cents_ != other.cents_; }
    bool operator<(const Money& other) const { return cents_ < other.cents_; }

    // Absolute difference in cents — used heavily by the reconciliation
    // engine's tolerance-based comparison (see §12 of the spec / Module 6).
    int64_t absDifferenceCents(const Money& other) const {
        int64_t diff = cents_ - other.cents_;
        return diff < 0 ? -diff : diff;
    }

private:
    int64_t cents_;
};

inline std::ostream& operator<<(std::ostream& os, const Money& m) {
    return os << m.toString();
}

} // namespace reconciliation::models
