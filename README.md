# Endgame

A UCI chess engine written from scratch in C++20: bitboard move generation, alpha-beta search and a hand-crafted evaluation tuned on the engine's own self-play games. It runs in any UCI chess GUI (Cute Chess, Arena, Banksia, En Croissant, ...) and supports standard chess and Chess960.

**Current release: 1.0.** Download a binary from [Releases](https://github.com/Krishiv-Vaid/endgame/releases) or build it yourself (below).

## Strength

Every number here comes from games, never from depth or speed.

| Build | Measurement | Result |
|---|---|---|
| Baseline search + Texel-tuned eval (pre-1.0) | Gauntlet vs Stash 11.0.1 – 17.0, anchored to their CCRL Blitz ratings, 988 games at 10+0.1 on an Apple M4 | **2045 ± 26** (95%) |
| + TT-move ordering | Self-play SPRT vs previous build, 8+0.08 | +147 ± 28 Elo |
| + static exchange evaluation | Self-play SPRT vs previous build, 8+0.08 | +9 ± 6 Elo |
| + killer moves | Self-play SPRT vs previous build, 8+0.08 | +69 ± 19 Elo |
| **1.0** (all of the above) | Not yet measured against rated engines | — |

Self-play gains usually overstate the gain against other engines, so 1.0's CCRL-scale rating is not simply 2045 + 225. A gauntlet of 1.0 against rated engines is the next measurement; results will be added to [docs/gauntlets.md](docs/gauntlets.md). The full test log, including failed and inconclusive tests, is in [PROGRESS.md](PROGRESS.md).

## Features

**Board and move generation**
- 12 piece bitboards plus a mailbox, incremental Zobrist keys (with separate pawn and non-pawn keys)
- Fancy magic bitboards for sliders (PEXT index in the `bmi2` build)
- Fully legal move generation using check and pin masks, including Chess960 castling
- Verified with perft on 126 standard and 960 Chess960 positions (every published depth), plus millions of fuzzed make/unmake round-trips

**Search**
- Iterative deepening, principal variation search
- Quiescence search over captures and queen promotions, with stand-pat, delta pruning and SEE pruning
- Transposition table: three entries per 32-byte cluster, depth/age replacement, mate scores stored ply-relative
- Move ordering: hash move, then winning captures (MVV-LVA, SEE ≥ 0), killer moves, quiet moves, losing captures
- Shallow-depth SEE pruning, mate distance pruning
- Draw detection: repetition, fifty-move rule, insufficient material
- Search on its own thread: `stop`, `ponderhit` and `isready` are answered immediately

**Evaluation**
- Tapered middlegame/endgame evaluation: material, piece-square tables, mobility, passed/isolated/doubled pawns, rooks on open files, bishop pair, pawn shield, king attack table, tempo
- 511 middlegame/endgame weight pairs fitted with a Texel tuner (`tools/texel`) on 4 million positions from the engine's own self-play games. The starting values came from simple formulas, not from another engine.

**Time management**
- Soft limit (no new iteration) and hard limit (abort), counting from the moment `go` arrives
- Keeps a reserve at low clocks, so it doesn't play on the increment alone; tested at 8+0.08 and 1+0.01

## UCI options

| Option | Default | Description |
|---|---|---|
| `Hash` | 16 | Transposition table size in MB (1 – 65536) |
| `Threads` | 1 | Search threads. 1.0 searches with one thread. |
| `Move Overhead` | 10 | Milliseconds reserved per move for communication delays |
| `Ponder` | false | Accepted; the GUI controls pondering with `go ponder` / `ponderhit` |
| `MultiPV` | 1 | Only 1 is supported in 1.0 |
| `UCI_Chess960` | false | Chess960 castling notation (king takes rook) |

## Extra commands

| Command | What it does |
|---|---|
| `bench [depth]` | Searches 50 fixed positions; prints `<nodes> nodes <nps> nps`. Every commit message records the node count. |
| `perft <depth>` / `divide <depth>` | Leaf-node count for the current position (divide: per move) |
| `d` | Prints the board, FEN, key and checkers |
| `eval` | Static evaluation of the current position |
| `datagen ...` | Self-play data generation for tuning (see `src/datagen.h`) |

From the command line, `endgame bench` runs one command and exits.

## Downloads

Pick the newest one your CPU supports:

| File | CPUs |
|---|---|
| `endgame-1.0-macos-arm64` | Apple Silicon Macs (M1 and later) |
| `endgame-1.0-linux-x86-64-avx512` / `windows-x86-64-avx512` | x86-64 with AVX-512 (e.g. Intel Ice Lake and later, AMD Zen 4 and later) |
| `endgame-1.0-linux-x86-64-avx2` / `windows-x86-64-avx2` | x86-64 with AVX2 and BMI1/2 (most CPUs since 2013–2015) |
| `endgame-1.0-linux-x86-64-v2` / `windows-x86-64-v2` | Any x86-64 CPU with SSE4.2 and POPCNT |

Linux and Windows binaries are statically linked. Each file has a `.sha256` next to it. On macOS, the first run may need `xattr -d com.apple.quarantine endgame-1.0-macos-arm64`, because the binary is not notarised.

## Building

Requires CMake ≥ 3.20, Ninja and a C++20 compiler (Apple Clang, LLVM Clang or GCC).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/endgame bench
```

`-DENDGAME_ARCH=` selects the target: `native` (default, `-mcpu=native` / `-march=native`), `armv8-neon`, `x86-64-v2`, `avx2`, `bmi2` (avx2 + PEXT sliders) or `avx512`. Other options: `ENDGAME_SANITIZE=ON` (ASan/UBSan), `ENDGAME_LTO` (on by default), `ENDGAME_PGO=generate|use`, `ENDGAME_STATIC=ON`.

## Testing

- `ctest --test-dir build`: unit tests, perft suites and make/unmake fuzzing. `ENDGAME_PERFT_MAX_NODES=0` runs every perft depth; `ENDGAME_FUZZ_MOVES=5000000` fuzzes longer.
- `tools/sprt.sh <dev-ref> <base-ref> [gainer|nonreg] [stc|ltc]`: builds two commits and runs a fastchess SPRT. Every strength change in this repository went through one.
- `tools/gauntlet.sh <ref> stash-14.0 ...`: plays CCRL-rated reference engines and fits an anchored rating with error bars.
- `tools/bench_compare.sh <a> <b>`: speed comparison of two builds.
- `tools/setup.sh` fetches and builds the external tools (fastchess, opening books).

CI builds and tests on macOS arm64, Linux x86-64 (GCC and Clang, x86-64-v2 / AVX2 / AVX-512, plus an ASan/UBSan build) and Windows (Clang).

## Roadmap

1.0 is the hand-crafted-evaluation engine. In progress:
- More search: history heuristics, null-move pruning, late move reductions, futility and late-move pruning, singular extensions and more, each SPRT-tested
- NNUE evaluation trained only on endgame's own self-play games (inference, data pipeline and trainer are already built and tested on a branch)
- Multithreaded search (Lazy SMP), Syzygy tablebases, MultiPV

## Acknowledgements

- The [Chess Programming Wiki](https://www.chessprogramming.org), for decades of documented ideas
- [fastchess](https://github.com/Disservin/fastchess), for matches and SPRTs
- [doctest](https://github.com/doctest/doctest) (MIT), the unit test framework
- [official-stockfish/books](https://github.com/official-stockfish/books), for the opening suites (UHO_Lichess_4852_v1, 8moves_v3)
- [CCRL](https://computerchess.org.uk/ccrl/404/), for the published ratings used as gauntlet anchors, and [Stash](https://github.com/mhouppin/stash-bot), the reference engine family used in gauntlets
- The perft test suites: the classic perftsuite originally distributed with ROCE, and the common 960-position Chess960 set

All engine code is original. No code, evaluation weights or training data come from other engines.

## License

MIT, see [LICENSE](LICENSE).
