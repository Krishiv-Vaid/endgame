#include "uci.h"

#include <iostream>
#include <sstream>
#include <string>

#include "init.h"
#include "perft.h"
#include "position.h"

namespace endgame {

namespace {

struct UciState {
    Position pos;
    bool chess960 = false;
};

// position [startpos | fen <fen>] [moves <m1> ...]
void cmd_position(UciState& u, std::istringstream& is) {
    std::string token, fen;
    is >> token;
    if (token == "startpos") {
        fen = Position::START_FEN;
        is >> token;  // consume "moves" if present
    } else if (token == "fen") {
        while (is >> token && token != "moves") fen += token + ' ';
    } else {
        return;
    }
    Position p;
    if (!p.set(fen, u.chess960)) {
        std::cout << "info string invalid fen" << std::endl;
        return;
    }
    while (is >> token) {
        Move m = p.uci_to_move(token);
        if (!m) {
            std::cout << "info string illegal move " << token << std::endl;
            break;
        }
        p.make_move(m);
    }
    u.pos = p;
}

// Returns false when the loop should terminate.
bool handle_command(UciState& u, const std::string& line) {
    std::istringstream is(line);
    std::string token;
    if (!(is >> token)) return true;

    if (token == "uci") {
        std::cout << "id name " << ENGINE_NAME << ' ' << ENGINE_VERSION << '\n'
                  << "id author " << ENGINE_AUTHOR << '\n'
                  << "option name UCI_Chess960 type check default false\n"
                  << "uciok" << std::endl;
    } else if (token == "isready") {
        std::cout << "readyok" << std::endl;
    } else if (token == "quit") {
        return false;
    } else if (token == "ucinewgame") {
        u.pos.set(Position::START_FEN, u.chess960);
    } else if (token == "setoption") {
        std::string name, value, t;
        is >> t;  // "name"
        while (is >> t && t != "value") name += (name.empty() ? "" : " ") + t;
        while (is >> t) value += (value.empty() ? "" : " ") + t;
        if (name == "UCI_Chess960") u.chess960 = value == "true";
    } else if (token == "position") {
        cmd_position(u, is);
    } else if (token == "d") {
        std::cout << u.pos.pretty() << std::flush;
    } else if (token == "perft" || token == "divide") {
        int depth = 1;
        is >> depth;
        Position copy = u.pos;
        divide(copy, depth, std::cout, token == "divide");
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
    init_all();
    UciState u;

    if (argc > 1) {
        std::string cmd;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) cmd += ' ';
            cmd += argv[i];
        }
        handle_command(u, cmd);
        return 0;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!handle_command(u, line)) break;
    }
    return 0;
}

}  // namespace endgame
