# endgame — progress log

Target: >3200 Elo on the CCRL Blitz scale (2'+1", 1 thread). See the build
brief for the rules; this file is the single source of truth for where the
project stands.

## Current state

| | |
|---|---|
| Best verified build | — |
| Bench | 0 (no search yet) |
| Latest gauntlet Elo | — |
| Default net | — |

## Milestones

- [x] **M0 — Scaffolding**: CMake, UCI stub, doctest, CI (macOS arm64, Linux GCC/Clang AVX2, Windows Clang) — done 2026-11-25, CI green on all 7 jobs
- [x] **M1 — Board & move generation**: FEN, make/unmake, magics, legal movegen, perft suite, fuzz test → tag `v0.1-movegen` — done 2026-11-25
- [ ] **M2 — Baseline search + HCE**: alpha-beta/PVS/qsearch/TT, time management, HCE, Texel tuner, 1000 clean games → tag `v0.2-search`
- [ ] **M3 — Testing infrastructure**: `tools/sprt.sh`, `tools/gauntlet.sh`, `tools/bench_compare.sh`
- [ ] **M4 — Search improvements** (one SPRT per feature) → tag `v0.3-hce`
- [ ] **M5 — NNUE + self-play loop** → tag `v1.0-nnue`
- [ ] **M6 — Polish to 3200+** → release `v2.0`

## Elo log

Every strength-affecting change, pass or fail.

| Date | Commit | Change | SPRT result | Gauntlet Elo |
|---|---|---|---|---|

## Notes and measurements

### M1 — move generation (2026-11-25, Apple M4, single thread, Release, `-mcpu=native`)

| Position | Depth | Nodes | Result | Speed (bulk counting) |
|---|---|---|---|---|
| startpos | 5 / 6 | 4,865,609 / 119,060,324 | match | ~410–530M nps |
| Kiwipete | 4 / 5 | 4,085,603 / 193,690,690 | match | ~450–590M nps |
| `8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -` | 6 | 11,030,083 | match | |
| `r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -` | 5 | 15,833,292 | match | |

- Standard perftsuite (126 positions, `tests/perft/standard.epd`): **all depths match**, uncapped (12.5 s).
- Chess960 suite (960 positions, `tests/perft/chess960.epd`): full uncapped run: **all 5,760 (position, depth) pairs match** (every depth to D6).
- Fuzz: 5,000,000+ make/unmake pairs from every suite position (5.19M assertions) — bitboards, mailbox, Zobrist/pawn/non-pawn keys, checkers and `gives_check` all consistent; unmake restores the exact FEN and key.
- The Kiwipete FEN in the build brief is missing the black pawn on h3 (`…/2N2Q2/…` should be `…/2N2Q1p/…`); with the correct FEN all published counts match.
- nps varies with thermal state and background load (fanless M4); range given is across runs.

## Next steps

1. M2: baseline search (alpha-beta, PVS, qsearch, TT, MVV-LVA), time management, HCE, bench.
2. Build fastchess; 1000-game self-play sanity run; Texel tuner on self-play positions.
