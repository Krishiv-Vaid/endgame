#include "uci.h"

#include <iostream>
#include <sstream>
#include <string>

namespace endgame {

namespace {

// Returns false when the loop should terminate.
bool handle_command(const std::string& line) {
    std::istringstream is(line);
    std::string token;
    if (!(is >> token)) return true;

    if (token == "uci") {
        std::cout << "id name " << ENGINE_NAME << ' ' << ENGINE_VERSION << '\n'
                  << "id author " << ENGINE_AUTHOR << '\n'
                  << "uciok" << std::endl;
    } else if (token == "isready") {
        std::cout << "readyok" << std::endl;
    } else if (token == "quit") {
        return false;
    } else if (token == "bench") {
        // No search yet: report an empty bench so tooling has something to parse.
        std::cout << "0 nodes 0 nps" << std::endl;
    } else {
        std::cout << "info string unknown command: " << token << std::endl;
    }
    return true;
}

}  // namespace

int uci_main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);

    if (argc > 1) {
        std::string cmd;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) cmd += ' ';
            cmd += argv[i];
        }
        handle_command(cmd);
        return 0;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!handle_command(line)) break;
    }
    return 0;
}

}  // namespace endgame
