#!/usr/bin/env bash
# SPRT a dev build against a base build with fastchess.
#   tools/sprt.sh <dev-ref> <base-ref> [gainer|nonreg] [stc|ltc] ["description"]
# gainer: elo0=0 elo1=5; nonreg: elo0=-5 elo1=0; alpha = beta = 0.05.
# STC 8+0.08, LTC 40+0.4; 1 thread, Hash 16, UHO_Lichess_4852_v1 openings.
# Prints the result and appends a row to PROGRESS.md's Elo log.
# Env: CONCURRENCY (default 8), BOOK, FASTCHESS, NO_LOG=1 to skip PROGRESS.md.
set -euo pipefail
root="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$root"
dev=${1:?usage: sprt.sh <dev> <base> [gainer|nonreg] [stc|ltc] [desc]}
base=${2:?usage: sprt.sh <dev> <base> [gainer|nonreg] [stc|ltc] [desc]}
kind=${3:-gainer}
tc_name=${4:-stc}
desc=${5:-$(git log -1 --format=%s "$dev")}
concurrency=${CONCURRENCY:-8}
book=${BOOK:-tools/books/UHO_Lichess_4852_v1.epd}
fastchess=${FASTCHESS:-tools/fastchess/fastchess}

case "$kind" in
  gainer) bounds="elo0=0 elo1=5" ;;
  nonreg) bounds="elo0=-5 elo1=0" ;;
  *) echo "kind must be gainer or nonreg" >&2; exit 2 ;;
esac
case "$tc_name" in
  stc) tc="8+0.08" ;;
  ltc) tc="40+0.4" ;;
  *) echo "tc must be stc or ltc" >&2; exit 2 ;;
esac

[ -x "$fastchess" ] || tools/setup.sh
dev_bin=$(tools/build_ref.sh "$dev")
base_bin=$(tools/build_ref.sh "$base")
dev_sha=$(git rev-parse --short=7 "$dev^{commit}")
base_sha=$(git rev-parse --short=7 "$base^{commit}")
for b in "$dev_bin" "$base_bin"; do echo "$b: $("$b" bench | tail -1)"; done

mkdir -p tools/results
tag="sprt-$dev_sha-vs-$base_sha-$tc_name"
out="tools/results/$tag.out"
echo "SPRT $dev_sha vs $base_sha ($kind, $tc_name $tc, concurrency $concurrency) -> $out"
caffeinate -dimsu "$fastchess" \
  -engine cmd="$dev_bin" name="dev-$dev_sha" \
  -engine cmd="$base_bin" name="base-$base_sha" \
  -each tc="$tc" option.Hash=16 option.Threads=1 \
  -openings file="$book" format=epd order=random \
  -games 2 -rounds 100000 -repeat -recover \
  -sprt $bounds alpha=0.05 beta=0.05 \
  -concurrency "$concurrency" \
  -pgnout file="tools/results/$tag.pgn" \
  -log file="tools/results/$tag.log" level=warn \
  > "$out" 2>&1 || true

elo=$(grep -E "^Elo:" "$out" | tail -1 | sed -E 's/Elo: ([-0-9.]+) \+\/- ([0-9.]+).*/\1 ± \2/')
llr=$(grep -E "^LLR:" "$out" | tail -1 | awk '{print $2}')
games=$(grep -E "^Games:" "$out" | tail -1 | sed -E 's/Games: ([0-9]+).*/\1/')
penta=$(grep -E "^Ptnml" "$out" | tail -1 | sed -E 's/Ptnml\(0-2\): //')
verdict="inconclusive"
if grep -q "H1 was accepted" "$out"; then verdict="PASSED"; fi
if grep -q "H0 was accepted" "$out"; then verdict="FAILED"; fi
summary="$(echo "$tc_name" | tr a-z A-Z) $tc [$bounds]: Elo $elo, LLR $llr, $games games, ptnml $penta — $verdict"
echo "$summary"

if [ "${NO_LOG:-0}" != "1" ]; then
  row="| $(date +%Y-%m-%d) | $dev_sha | $desc | $summary | |"
  python3 - "$row" <<'PY'
import sys, re
row = sys.argv[1]
p = "PROGRESS.md"
s = open(p).read()
marker = "|---|---|---|---|---|\n"
i = s.index("## Elo log")
j = s.index(marker, i) + len(marker)
# append after the last table row
k = j
while k < len(s) and s[k] == "|":
    k = s.index("\n", k) + 1
s = s[:k] + row + "\n" + s[k:]
open(p, "w").write(s)
PY
fi
[ "$verdict" = "PASSED" ]
