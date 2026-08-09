#include "cli/Cli.hpp"

// main() is deliberately thin: all argument parsing and command dispatch
// lives in cli::Cli (Module 6). Modules 1-5 built the engine; this file's
// only remaining job is to hand control to the CLI and return its exit
// code.
int main(int argc, char** argv) {
    reconciliation::cli::Cli cli;
    return cli.run(argc, argv);
}
