#!/usr/bin/env python3
"""Anchored rating of one engine from gauntlet PGNs.

    rate.py <games.pgn>... --engine NAME [--anchors tools/gauntlet/ccrl_blitz.csv]

Opponents are fixed at their CCRL Blitz ratings (looked up by PGN player
name "<engine>-<version>"). The tested engine's rating R maximises the
likelihood of the observed scores under the logistic Elo model
P(score) = 1 / (1 + 10^(-(R - r_opp) / 400)), draws counting as half a
point. The 95% interval combines the statistical error (from the Fisher
information) with the anchors' own published error bars.
Equivalent to a single-player Ordo/BayesElo fit with fixed anchors.
"""
import argparse, csv, math, re, sys
from collections import defaultdict

def load_anchors(path):
    anchors = {}
    with open(path) as f:
        for row in csv.reader(l for l in f if not l.startswith("#")):
            name, ver, _tag, rating, err, _games = row
            anchors[f"{name}-{ver}"] = (float(rating), float(err))
    return anchors

def games(paths):
    for p in paths:
        text = open(p, errors="replace").read()
        for g in text.split("[Event ")[1:]:
            tags = dict(re.findall(r'\[(\w+) "([^"]*)"\]', g))
            if tags.get("Result") in ("1-0", "0-1", "1/2-1/2"):
                yield tags["White"], tags["Black"], tags["Result"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pgn", nargs="+")
    ap.add_argument("--engine", required=True)
    ap.add_argument("--anchors", default="tools/gauntlet/ccrl_blitz.csv")
    a = ap.parse_args()
    anchors = load_anchors(a.anchors)
    score = defaultdict(float); n = defaultdict(int)
    wdl = defaultdict(lambda: [0, 0, 0])
    for w, b, res in games(a.pgn):
        if a.engine not in (w, b): continue
        opp = b if w == a.engine else w
        s = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}[res]
        if b == a.engine: s = 1 - s
        score[opp] += s; n[opp] += 1
        wdl[opp][0 if s == 1 else 1 if s == 0.5 else 2] += 1
    opps = [o for o in n if o in anchors]
    missing = [o for o in n if o not in anchors]
    if missing: print("no anchor rating for:", ", ".join(missing), file=sys.stderr)
    if not opps: sys.exit("no anchored games")

    c = math.log(10) / 400
    def grad_hess(R):
        g = h = 0.0
        for o in opps:
            p = 1 / (1 + 10 ** (-(R - anchors[o][0]) / 400))
            g += c * (score[o] - n[o] * p)
            h += c * c * n[o] * p * (1 - p)
        return g, h
    R = sum(anchors[o][0] * n[o] for o in opps) / sum(n[o] for o in opps)
    for _ in range(100):
        g, h = grad_hess(R)
        step = g / h
        R += max(-200, min(200, step))
        if abs(step) < 1e-6: break
    _, h = grad_hess(R)
    stat = 1.96 / math.sqrt(h)
    total = sum(n[o] for o in opps)
    anchor_err = math.sqrt(sum((n[o] / total * anchors[o][1]) ** 2 for o in opps))
    err = math.sqrt(stat ** 2 + anchor_err ** 2)

    print(f"{'opponent':<16}{'CCRL':>6}{'games':>7}{'W-D-L':>14}{'score':>8}{'perf':>7}")
    for o in sorted(opps, key=lambda o: anchors[o][0]):
        sc = score[o] / n[o]
        perf = anchors[o][0] + (400 * math.log10(sc / (1 - sc)) if 0 < sc < 1 else float("nan"))
        w_, d_, l_ = wdl[o]
        print(f"{o:<16}{anchors[o][0]:>6.0f}{n[o]:>7}{f'{w_}-{d_}-{l_}':>14}{sc*100:>7.1f}%{perf:>7.0f}")
    print(f"\n{a.engine}: {R:.0f} ± {err:.0f} (95%; statistical ±{stat:.0f}, anchors ±{anchor_err:.0f}), {total} games")

if __name__ == "__main__":
    main()
