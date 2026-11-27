#include "uci.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "bench.h"
#include "datagen.h"
#include "packed.h"
#include "eval_hce.h"
#include "init.h"
#include "nnue/network.h"
#include "nnue/simd.h"
#include "perft.h"
#include "position.h"
#include "search.h"

namespace endgame {

namespace {

struct UciState {
    Engine engine;
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

SearchLimits parse_go(std::istringstream& is) {
    SearchLimits l;
    l.start_time = now_ms();  // clock starts when the command arrives
    std::string token;
    auto read = [&is](auto& v) {
        long long x = 0;
        if (is >> x) v = std::max(0LL, x);
    };
    while (is >> token) {
        if (token == "wtime") read(l.time[WHITE]);
        else if (token == "btime") read(l.time[BLACK]);
        else if (token == "winc") read(l.inc[WHITE]);
        else if (token == "binc") read(l.inc[BLACK]);
        else if (token == "movestogo") read(l.movestogo);
        else if (token == "depth") read(l.depth);
        else if (token == "nodes") read(l.nodes);
        else if (token == "movetime") read(l.movetime);
        else if (token == "infinite") l.infinite = true;
        else if (token == "ponder") l.ponder = true;
    }
    if (!l.use_time() && !l.depth && !l.nodes) l.infinite = true;
    return l;
}

void cmd_setoption(UciState& u, std::istringstream& is) {
    std::string name, value, t;
    is >> t;  // "name"
    while (is >> t && t != "value") name += (name.empty() ? "" : " ") + t;
    while (is >> t) value += (value.empty() ? "" : " ") + t;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    auto as_int = [&](int lo, int hi, int def) {
        try {
            return std::clamp(std::stoi(value), lo, hi);
        } catch (...) {
            return def;
        }
    };
    if (lower == "hash") u.engine.set_hash(std::size_t(as_int(1, 65536, 16)));
    else if (lower == "move overhead") u.engine.move_overhead = as_int(0, 5000, 10);
    else if (lower == "uci_chess960") u.chess960 = value == "true";
    else if (lower == "evalfile") {
        if (value.empty() || value == "<empty>" || value == "hce") {
            nnue::unload();
            std::cout << "info string using hand-crafted evaluation" << std::endl;
        } else {
            std::string err;
            if (nnue::load_file(value, &err))
                std::cout << "info string loaded network " << value << " (hash " << std::hex
                          << nnue::active_hash() << std::dec << ", " << nnue::simd::name() << ")" << std::endl;
            else
                std::cout << "info string could not load " << value << ": " << err << std::endl;
        }
        u.engine.new_game();  // TT evals came from the old evaluator
    }
    else if (lower == "threads" || lower == "ponder" || lower == "multipv") {
        // Accepted; single-threaded search and MultiPV 1 for now.
    } else std::cout << "info string unknown option " << name << std::endl;
}

// Returns false when the loop should terminate.
bool handle_command(UciState& u, const std::string& line) {
    std::istringstream is(line);
    std::string token;
    if (!(is >> token)) return true;

    if (token == "uci") {
        std::cout << "id name " << ENGINE_NAME << ' ' << ENGINE_VERSION << '\n'
                  << "id author " << ENGINE_AUTHOR << '\n'
                  << "option name Hash type spin default 16 min 1 max 65536\n"
                  << "option name Threads type spin default 1 min 1 max 1\n"
                  << "option name Move Overhead type spin default 10 min 0 max 5000\n"
                  << "option name Ponder type check default false\n"
                  << "option name MultiPV type spin default 1 min 1 max 1\n"
                  << "option name UCI_Chess960 type check default false\n"
                  << "option name EvalFile type string default <empty>\n"
                  << "uciok" << std::endl;
    } else if (token == "isready") {
        std::cout << "readyok" << std::endl;
    } else if (token == "quit") {
        u.engine.stop();
        return false;
    } else if (token == "stop") {
        u.engine.stop();
    } else if (token == "ponderhit") {
        u.engine.ponderhit();
    } else if (token == "ucinewgame") {
        u.engine.stop();
        u.engine.new_game();
        u.pos.set(Position::START_FEN, u.chess960);
    } else if (token == "setoption") {
        u.engine.stop();
        cmd_setoption(u, is);
    } else if (token == "position") {
        u.engine.stop();
        cmd_position(u, is);
    } else if (token == "go") {
        u.engine.stop();
        u.engine.go(u.pos, parse_go(is));
    } else if (token == "d") {
        std::cout << u.pos.pretty() << std::flush;
    } else if (token == "eval") {
        std::cout << "hce  (side to move): " << evaluate(u.pos) << " cp" << std::endl;
        if (const nnue::Network* net = nnue::active()) {
            nnue::Accumulator acc;
            nnue::refresh(*net, u.pos, acc);
            std::cout << "nnue (side to move): " << nnue::output(*net, acc, u.pos.side_to_move())
                      << " cp" << std::endl;
        }
    } else if (token == "perft" || token == "divide") {
        int depth = 1;
        is >> depth;
        Position copy = u.pos;
        divide(copy, depth, std::cout, token == "divide");
    } else if (token == "datagen") {
        // datagen [threads N] [positions N] [nodes N] [hardnodes N] [plies N] [seed N] [out PREFIX]
        DatagenOptions o;
        std::string key;
        while (is >> key) {
            if (key == "out") is >> o.output;
            else if (key == "threads") is >> o.threads;
            else if (key == "positions") is >> o.positions;
            else if (key == "nodes") is >> o.soft_nodes;
            else if (key == "hardnodes") is >> o.hard_nodes;
            else if (key == "plies") is >> o.random_plies;
            else if (key == "seed") is >> o.seed;
            else if (key == "format") {
                std::string f;
                is >> f;
                o.binary = f != "txt";
            }
        }
        u.engine.stop();
        run_datagen(o);
    } else if (token == "txt2bin") {
        // txt2bin <out.bin> <in.txt>...: convert "fen | score | result" lines.
        std::string out, path, line;
        is >> out;
        ShardWriter w(out);
        std::vector<PackedPosition> buf;
        std::uint64_t bad = 0;
        while (is >> path) {
            std::ifstream in(path);
            while (std::getline(in, line)) {
                auto a = line.find('|'), b = line.rfind('|');
                Position p;
                if (a == std::string::npos || a == b || !p.set(line.substr(0, a))) {
                    ++bad;
                    continue;
                }
                int score = std::stoi(line.substr(a + 1, b - a - 1));
                double res = std::stod(line.substr(b + 1));
                buf.push_back(PackedPosition::pack(p, score, res > 0.75 ? 2 : res < 0.25 ? 0 : 1));
                if (buf.size() == 65536) w.write(buf.data(), buf.size()), buf.clear();
            }
        }
        w.write(buf.data(), buf.size());
        std::cout << "wrote " << w.count() << " positions to " << out << " (" << bad << " bad lines)" << std::endl;
    } else if (token == "shardinfo") {
        // shardinfo <file>...: verify shards and report record counts.
        std::string path;
        std::uint64_t total = 0;
        while (is >> path) {
            std::vector<PackedPosition> recs;
            std::string err;
            if (read_shard(path, recs, &err)) {
                std::cout << path << ": " << recs.size() << " positions, checksum ok" << std::endl;
                total += recs.size();
            } else {
                std::cout << path << ": INVALID (" << err << ")" << std::endl;
            }
        }
        std::cout << "total " << total << " positions" << std::endl;
    } else if (token == "bench") {
        int depth = 0;
        is >> depth;
        u.engine.stop();
        run_bench(u.engine, depth);
    } else {
        std::cout << "info string unknown command: " << token << std::endl;
    }
    return true;
}

}  // namespace

int uci_main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    init_all();
    auto u = std::make_unique<UciState>();

    if (argc > 1) {
        std::string cmd;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) cmd += ' ';
            cmd += argv[i];
        }
        handle_command(*u, cmd);
        u->engine.wait();
        return 0;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!handle_command(*u, line)) break;
    }
    u->engine.stop();
    return 0;
}

}  // namespace endgame
