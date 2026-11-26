#!/usr/bin/env bash
# One-time setup of external test tooling (all gitignored):
#   tools/fastchess/fastchess   built from source
#   tools/books/*.epd|pgn       official-stockfish/books opening suites
set -euo pipefail
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)/tools"
if [ ! -x fastchess/fastchess ]; then
  [ -d fastchess ] || git clone -q --depth 1 https://github.com/Disservin/fastchess.git
  make -C fastchess -j"${BUILD_JOBS:-8}" >/dev/null
fi
mkdir -p books
for b in UHO_Lichess_4852_v1.epd 8moves_v3.pgn; do
  if [ ! -f "books/$b" ]; then
    curl -sSfL -o "books/$b.zip" "https://github.com/official-stockfish/books/raw/master/$b.zip"
    unzip -o -q "books/$b.zip" -d books && rm "books/$b.zip"
  fi
done
echo "tools ready: $(fastchess/fastchess --version 2>&1 | head -1)"
