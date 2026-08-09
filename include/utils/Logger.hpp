#pragma once

#include <string>

namespace reconciliation::utils {

// ---------------------------------------------------------------------------
// Logger
//
// WHY hand-rolled instead of spdlog/glog/etc.?
//   The project's logging needs are simple: three severity levels, a
//   timestamp, and stdout output. Pulling in a third-party logging
//   library would add a build dependency (and, on Windows, another
//   thing to get building via CMake/vcpkg) for a feature that's about
//   40 lines of code. This is a deliberate "avoid unnecessary
//   dependencies" decision (see §21 of the spec) — not an implication
//   that real production systems shouldn't use spdlog/glog (they
//   usually should, for things like async logging and log rotation).
//
// Thread-safety note: as of Module 1 the project is single-threaded, so
// no locking is implemented. If the optional multithreading extension
// (§25) is built later, this class will need a mutex around the output
// stream — that's called out explicitly in docs/FUTURE_IMPROVEMENTS.md.
// ---------------------------------------------------------------------------
enum class LogLevel { INFO, WARN, ERROR };

class Logger {
public:
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);

private:
    static void log(LogLevel level, const std::string& message);
    static std::string levelToString(LogLevel level);
    static std::string currentTimestamp();
};

} // namespace reconciliation::utils
