#pragma once

#include <string>

namespace reconciliation::utils {

// ---------------------------------------------------------------------------
// Sha256
//
// WHY implement this instead of using OpenSSL's SHA256?
//   We already link libpq (which itself depends on OpenSSL for TLS), so
//   OpenSSL's EVP interface would technically be "free" to use. But
//   pulling in <openssl/evp.h> just for one hash function creates a
//   second, unrelated reason the build could fail on a machine that has
//   libpqxx but a different/missing OpenSSL dev package — for a single,
//   well-specified, side-effect-free algorithm like SHA-256, a ~150 line
//   self-contained implementation is more robust across platforms than
//   an extra dependency edge. This is the same "avoid unnecessary
//   dependencies" reasoning as Logger and ConfigLoader.
//
// WHAT IT'S USED FOR: hashing ingested CSV file contents so
// ingestion_batches can detect "this exact file was already ingested"
// (idempotency, spec §46) without relying on filename or file size, both
// of which are easy to spoof or coincidentally collide.
//
// This implementation follows FIPS 180-4 directly and has been verified
// against the standard test vectors — notably sha256("") and sha256("abc")
// — before being used anywhere in the ingestion pipeline.
// ---------------------------------------------------------------------------
class Sha256 {
public:
    // Returns the lowercase hex digest (64 characters) of the given bytes.
    static std::string hash(const std::string& data);
};

} // namespace reconciliation::utils
