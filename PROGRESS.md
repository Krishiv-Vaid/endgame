# endgame — progress log

Target: >3200 Elo on the CCRL Blitz scale (2'+1", 1 thread). See the build
brief for the rules; this file is the single source of truth for where the
project stands.

## Current state

| | |
|---|---|
| Best verified build | 67c49f6 (M2 + TT-move + SEE + killers) |
| Bench | 5004669 (depth 6) |
| Latest gauntlet Elo | **2045 ± 26** CCRL Blitz (M4, 10+0.1, vs Stash 11–17; sleep forfeits excluded; 2016 ± 24 raw) — see docs/gauntlets.md |
| Default net | — |

## Releases

- **v1.0 (2026-11-27)** — shipped at the user's request from `main` at the strongest SPRT-verified build: M2 baseline
  + Texel gen-1 HCE + TT-move ordering + SEE + killers (bench 5004669). This supersedes the brief's planned
  `v1.0-nnue` / `v2.0` naming: later milestone tags continue as v1.x / v2.0. CCRL-scale strength of v1.0 itself is not
  yet measured (last gauntlet 2045 ± 26 was the pre-TT-move M2 build).

## Milestones

- [x] **M0 — Scaffolding**: CMake, UCI stub, doctest, CI (macOS arm64, Linux GCC/Clang AVX2, Windows Clang) — done 2026-11-25, CI green on all 7 jobs
- [x] **M1 — Board & move generation**: FEN, make/unmake, magics, legal movegen, perft suite, fuzz test → tag `v0.1-movegen` — done 2026-11-25
- [x] **M2 — Baseline search + HCE**: alpha-beta/PVS/qsearch/TT, time management, HCE, Texel tuner, 1000 clean games → tag `v0.2-search` — done 2026-11-26, gauntlet 2045 ± 26
- [x] **M3 — Testing infrastructure**: `tools/sprt.sh`, `tools/gauntlet.sh`, `tools/bench_compare.sh` — done 2026-11-26 (first full SPRT run end to end with sprt.sh)
- [ ] **M4 — Search improvements** (one SPRT per feature) → tag `v0.3-hce`
- [ ] **M5 — NNUE + self-play loop** → tag `v1.0-nnue`
- [ ] **M6 — Polish to 3200+** → release `v2.0`

## Elo log

Every strength-affecting change, pass or fail.

| Date | Commit | Change | SPRT result | Gauntlet Elo |
|---|---|---|---|---|
| 2026-11-26 | 266a219 (merged as 609cdac) | eval: Texel-tuned HCE weights (gen 1), 4.0M self-play positions | STC 8+0.08 [0, 5]: Elo +95.3 ± 23.6, LLR 2.96, 762 games, ptnml [33, 35, 127, 67, 119] — **PASSED**. 24 games forfeited when the Mac slept on battery (lid closed); excluding those pairs: +97.9 ± 24.1 over 736 games | 2045 ± 26 |
| 2026-11-26 | e7fc6eb | search: try the TT move first | STC 8+0.08 [0, 5]: Elo +147.2 ± 28.2, LLR 2.97, 520 games, ptnml [11, 18, 74, 66, 91] — **PASSED**. 8 games lost to a clamshell sleep; excluding those pairs: +154.7 ± 28.1 over 512 games | |
| 2026-11-27 | 4397ac1 | search: SEE for ordering (bad captures after quiets) + pruning in qsearch and at depth <= 6 | STC 8+0.08 [0, 5]: Elo +9.1 ± 6.2, LLR 2.95, 8,480 games, ptnml [521, 705, 1647, 764, 603] — **PASSED**. 2 dev time losses (105/170 ms) coincided with a lid close at 22:19 (system stall), no sleep forfeits | |
| 2026-11-27 | 67c49f6 | search: killer moves (2 per ply) | STC 8+0.08 [0, 5]: Elo +68.6 ± 18.7, LLR 2.95, 908 games, ptnml [25, 64, 169, 101, 95] — **PASSED**, no time losses | |

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

### M2 — baseline search (2026-11-25)

- Bench: 44,570,288 nodes at depth 6 (50 positions), ~7.1M nps on the M4. Deterministic across runs.
- Sanity matches (self-play, 8+0.08, 8 concurrent, UHO openings) found three bugs before a clean run:
  1. 12 ms time loss: the clock started when the search thread started, not when `go` arrived. Fixed.
  2. Crash (SIGBUS): iterative deepening reached depth 228 in a mate-in-1 position and overflowed macOS's 512 KB
     secondary-thread stack. Search/datagen threads now get a 16 MB stack; regression test added.
  3. Three 1–15 ms time losses: the bank drained to ~100 ms living on the increment, leaving too little slack for OS
     jitter. The increment share now shrinks below 10 increments of bank, hard cap 50% of usable time.
- **Clean sanity run (commit a978db1): 1000 games, 0 time losses, 0 illegal moves, 0 crashes/stalls.** All games ended
  by mate (795) or a rules draw (126 repetition, 45 insufficient material, 31 fifty-move, 3 stalemate). Self-play
  score 48.35% (Elo −11.5 ± 11.4, same binary both sides: noise). `tools/results/sanity-a978db1.*` (not committed).
- Ordering in the baseline is MVV-LVA only; TT-move ordering is deliberately left as the first M4 SPRT.

## Next steps

1. M4, in the brief's order, one branch + one SPRT each: ~~TT-move ordering~~ (+147), ~~SEE~~ (+9), ~~killers~~ (+69), counter-moves (next), counter-moves, history, null move, LMR, RFP, ...
2. Long runs need AC power and the lid open: on battery macOS sleeps on lid close and forfeits every running game.
