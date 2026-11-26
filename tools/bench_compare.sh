#!/usr/bin/env bash
# Compare bench speed of two builds (for speedup / non-functional changes).
#   tools/bench_compare.sh <ref-a> <ref-b> [runs=10]
# Runs alternate a/b to share thermal state; prints mean nps, the speedup of
# b over a with a 95% interval, and whether the node counts match.
set -euo pipefail
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
a=$(tools/build_ref.sh "${1:?usage: bench_compare.sh <a> <b> [runs]}")
b=$(tools/build_ref.sh "${2:?usage: bench_compare.sh <a> <b> [runs]}")
runs=${3:-10}
tmp=$(mktemp)
for i in $(seq "$runs"); do
  echo "a $("$a" bench | tail -1)" >> "$tmp"
  echo "b $("$b" bench | tail -1)" >> "$tmp"
done
python3 - "$tmp" "$a" "$b" <<'PY'
import sys, statistics as st
rows = [l.split() for l in open(sys.argv[1])]
nps = {"a": [], "b": []}; nodes = {"a": set(), "b": set()}
for r in rows:
    nps[r[0]].append(float(r[3])); nodes[r[0]].add(r[1])
ma, mb = st.mean(nps["a"]), st.mean(nps["b"])
ratios = [y / x - 1 for x, y in zip(nps["a"], nps["b"])]
mr = st.mean(ratios); err = 1.96 * st.stdev(ratios) / len(ratios) ** 0.5 if len(ratios) > 1 else 0
print(f"a {sys.argv[2]}: {ma:,.0f} nps  bench {','.join(nodes['a'])}")
print(f"b {sys.argv[3]}: {mb:,.0f} nps  bench {','.join(nodes['b'])}")
print(f"speedup b vs a: {mr*100:+.2f}% ± {err*100:.2f}% ({len(ratios)} paired runs)")
print("bench nodes", "MATCH" if nodes["a"] == nodes["b"] else "DIFFER")
PY
rm -f "$tmp"
