#!/usr/bin/env python3
"""Recompute an SPRT's Elo from its PGN, excluding game pairs where either
game was forfeited on time by more than --min-overrun ms (machine sleep, not
engine time management). Prints both the full and the cleaned estimate.
    sprt_clean.py <sprt.pgn> [--min-overrun 1000]"""
import re, sys, math
from collections import defaultdict
path = sys.argv[1]
limit = int(sys.argv[sys.argv.index("--min-overrun") + 1]) if "--min-overrun" in sys.argv else 1000
pairs = defaultdict(list)
for g in open(path, errors="replace").read().split("[Event ")[1:]:
    t = dict(re.findall(r'\[(\w+) "([^"]*)"\]', g))
    res = t.get("Result")
    if res not in ("1-0", "0-1", "1/2-1/2"):
        continue
    s = {"1-0": 1, "0-1": 0, "1/2-1/2": 0.5}[res]
    if t["Black"].startswith("dev"):
        s = 1 - s
    m = re.search(r"loses on time \((\d+)ms overrun\)", g)
    pairs[int(t["Round"])].append((s, bool(m and int(m.group(1)) > limit)))

def stats(ps):
    penta = [0] * 5
    for p in ps:
        penta[int(round(2 * sum(x[0] for x in p)))] += 1
    n = sum(penta)
    mu = sum(i * c for i, c in enumerate(penta)) / n / 4
    var = sum(c * (i / 4 - mu) ** 2 for i, c in enumerate(penta)) / n
    se = math.sqrt(var / n)
    elo = lambda x: -400 * math.log10(1 / x - 1)
    return penta, 2 * n, elo(mu), (elo(mu + 1.96 * se) - elo(mu - 1.96 * se)) / 2

full = [p for p in pairs.values() if len(p) == 2]
clean = [p for p in full if not any(x[1] for x in p)]
for name, ps in (("all pairs", full), (f"excluding pairs with >{limit} ms overruns", clean)):
    pen, n, e, err = stats(ps)
    print(f"{name}: {n} games, ptnml {pen}, Elo {e:+.1f} ± {err:.1f}")
