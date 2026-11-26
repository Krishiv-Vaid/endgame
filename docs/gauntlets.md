# Gauntlet results

Anchored to CCRL Blitz ratings (`tools/gauntlet/ccrl_blitz.csv`, list dated
2026-11-21). Unless noted, games are played on the Apple M4 at a faster time
control than CCRL's 2'+1", single thread, Hash 16, balanced openings; ratings
are a maximum-likelihood fit with the opponents fixed at their CCRL values
(`tools/gauntlet/rate.py`). Error bars are 95% and include the anchors' own
published errors.

## 2026-11-26 — M2 HCE baseline (609cdac, Texel gen 1)

10+0.1 on the Apple M4, 8 concurrent, 8moves_v3 openings, 200 games per opponent.

| Opponent | CCRL Blitz | W-D-L | Score | Perf |
|---|---|---|---|---|
| Stash 11.0.1 | 1687 | 132-21-47 | 71.2% | 1845 |
| Stash 12.0 | 1882 | 106-21-73 | 58.2% | 1940 |
| Stash 13.0.1 | 1966 | 97-27-76 | 55.2% | 2003 |
| Stash 14.0 | 2057 | 79-35-86 | 48.2% | 2045 |
| Stash 15.3 | 2172 | 82-17-101 | 45.2% | 2139 |
| Stash 17.0 | 2296 | 37-22-141 | 24.0% | 2096 |

- All 1200 games: **2016 ± 24**.
- The Mac slept repeatedly overnight (lid closed on battery), forfeiting 212 games on time with multi-minute
  overruns (endgame lost 138 of them, opponents 74). Excluding those games: **2045 ± 26** over 988 games.
  This is the representative number; the raw one is kept for honesty.

