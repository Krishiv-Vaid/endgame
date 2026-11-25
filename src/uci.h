#pragma once

#include <iosfwd>
#include <string>

namespace endgame {

inline constexpr const char* ENGINE_NAME = "endgame";
inline constexpr const char* ENGINE_VERSION = "0.0.1-dev";
inline constexpr const char* ENGINE_AUTHOR = "Krishiv Vaid";

// Runs the UCI command loop until "quit" or EOF. If argc > 1 the command-line
// arguments are executed as a single command and the function returns
// (e.g. `endgame bench`).
int uci_main(int argc, char** argv);

}  // namespace endgame
