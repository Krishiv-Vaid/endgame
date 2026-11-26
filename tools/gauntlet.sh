#!/usr/bin/env bash
# Gauntlet: one endgame build vs CCRL-rated reference engines, then an
# anchored rating with error bars.
#   tools/gauntlet.sh <ref> <opponent>... (opponents as engine-version, e.g. stash-14.0)
# Env: GAMES per opponent (default 200), TC (default 10+0.1), CONCURRENCY (8),
#      BOOK (default 8moves_v3.pgn, balanced), NO_LOG=1.
# Note: CCRL rates at 2'+1" on its own hardware; we play faster on the M4,
# which is a documented approximation, and anchor to their published ratings.
set -euo pipefail
root="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$root"
ref=${1:?usage: gauntlet.sh <ref> <opponent>...}; shift
[ $# -gt 0 ] || { echo "need at least one opponent" >&2; exit 2; }
games=${GAMES:-200}; tc=${TC:-10+0.1}; conc=${CONCURRENCY:-8}
book=${BOOK:-tools/books/8moves_v3.pgn}
fmt=pgn; [[ "$book" == *.epd ]] && fmt=epd
fastchess=${FASTCHESS:-tools/fastchess/fastchess}
[ -x "$fastchess" ] || tools/setup.sh

dev=$(tools/build_ref.sh "$ref")
sha=$(git rev-parse --short=7 "$ref^{commit}")
name="endgame-$sha"
args=(-engine cmd="$dev" name="$name")
for o in "$@"; do
  bin=$(tools/gauntlet/build_engine.sh "${o%-*}" "${o##*-}")
  args+=(-engine cmd="$bin" name="$o")
done
mkdir -p tools/results
tag="gauntlet-$sha-$(date +%Y%m%d-%H%M)"
echo "gauntlet $name vs $* ($games games each, tc $tc) -> tools/results/$tag.*"
caffeinate -dimsu "$fastchess" "${args[@]}" \
  -tournament gauntlet -seeds 1 \
  -each tc="$tc" option.Hash=16 option.Threads=1 \
  -openings file="$book" format=$fmt order=random \
  -rounds $((games / 2)) -games 2 -repeat -recover -concurrency "$conc" \
  -pgnout file="tools/results/$tag.pgn" -log file="tools/results/$tag.log" level=warn \
  > "tools/results/$tag.out" 2>&1 || true
python3 tools/gauntlet/rate.py "tools/results/$tag.pgn" --engine "$name" | tee "tools/results/$tag.rating"
if [ "${NO_LOG:-0}" != "1" ]; then
  line=$(tail -1 "tools/results/$tag.rating")
  echo "- $(date +%Y-%m-%d) gauntlet $sha (tc $tc, M4): $line vs $*" >> docs/gauntlets.md
fi
