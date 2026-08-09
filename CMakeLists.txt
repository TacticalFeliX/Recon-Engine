cmake_minimum_required(VERSION 3.16)
project(reconciliation_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# WHY C++17 AND NOT C++20?
# C++20 was the initial choice, but building against it surfaced a real,
# verified ABI incompatibility: Ubuntu 24.04's packaged libpqxx-dev (7.8.1)
# ships a libpqxx.so that was itself compiled without std::source_location
# support. libpqxx's public headers conditionally add a
# std::source_location parameter to several exception constructors based
# on the __cpp_lib_source_location feature-test macro, which IS defined
# when a translation unit compiles with -std=c++20. The result: code
# compiled with -std=c++20 against these headers expects 2-argument
# exception constructors that don't exist in the pre-built .so, producing
# "undefined reference" linker errors at link time.
#
# This was reproduced and diagnosed directly (checked exported symbols
# with `nm`) rather than assumed. Building libpqxx from source with
# matching flags would resolve it, but that adds a real dependency-build
# step for every contributor and CI run. Since nothing in this codebase
# uses a C++20-only language feature, C++17 avoids the problem entirely
# while keeping the exact same libpqxx package installable via a single
# `apt install libpqxx-dev` (or `vcpkg install libpqxx` on Windows).
# ---------------------------------------------------------------------------

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# libpqxx discovery -- two supported paths, tried in order:
#   1. CMake config package (what vcpkg provides on Windows).
#   2. pkg-config (what Ubuntu/Debian's libpqxx-dev package provides via
#      apt -- the path this project was actually built and tested against).
# Not hand-writing find_library() calls for libpqxx/libpq: both paths
# above are more reliable across platforms than guessing search paths.
# ---------------------------------------------------------------------------
find_package(libpqxx CONFIG QUIET)

if (NOT libpqxx_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCES
    src/main.cpp
    src/cli/Cli.cpp
    src/utils/Logger.cpp
    src/utils/Sha256.cpp
    src/ingestion/CsvParser.cpp
    src/ingestion/RowValidator.cpp
    src/ingestion/StreamingCsvIngestor.cpp
    src/database/ConfigLoader.cpp
    src/database/DatabaseConnection.cpp
    src/database/SchemaManager.cpp
    src/database/IngestionBatchRepository.cpp
    src/database/IngestionErrorRepository.cpp
    src/database/TransactionRepository.cpp
    src/database/ReconciliationRunRepository.cpp
    src/database/ReconciliationResultRepository.cpp
    src/reconciliation/MatchClassifier.cpp
    src/reconciliation/BruteForceReconciler.cpp
    src/reconciliation/TwoPointerReconciler.cpp
    src/reconciliation/HashReconciler.cpp
    src/reporting/CsvReportWriter.cpp
)

add_executable(reconcile ${SOURCES})

if (libpqxx_FOUND)
    target_link_libraries(reconcile PRIVATE libpqxx::pqxx)
else()
    target_link_libraries(reconcile PRIVATE PkgConfig::PQXX)
endif()
