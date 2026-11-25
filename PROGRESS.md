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

- [ ] **M0 — Scaffolding**: CMake, UCI stub, doctest, CI (macOS arm64, Linux GCC/Clang AVX2, Windows Clang)
- [ ] **M1 — Board & move generation**: FEN, make/unmake, magics, legal movegen, perft suite, fuzz test → tag `v0.1-movegen`
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

## Next steps

1. Finish M0: get CI green on all runners.
2. M1: bitboards, magics, position, legal movegen, perft suite.
