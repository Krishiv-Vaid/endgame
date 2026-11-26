#!/usr/bin/env bash
# Build a git ref into tools/engines/endgame-<sha> (cached) and print the path.
#   tools/build_ref.sh <ref> [arch]
set -euo pipefail
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
ref=${1:?usage: build_ref.sh <ref> [arch]}
arch=${2:-native}
sha=$(git rev-parse --short=7 "$ref^{commit}")
out="tools/engines/endgame-$sha${2:+-$arch}"
mkdir -p tools/engines
if [ ! -x "$out" ]; then
  wt=$(mktemp -d "${TMPDIR:-/tmp}/endgame-wt.XXXXXX")
  git worktree add -q --detach "$wt" "$sha"
  jobs=${BUILD_JOBS:-4}
  cmake -S "$wt" -B "$wt/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DENDGAME_ARCH="$arch" \
        -DENDGAME_TESTS=OFF -DENDGAME_TOOLS=OFF >/dev/null
  cmake --build "$wt/build" -j "$jobs" >/dev/null
  cp "$wt/build/endgame" "$out"
  git worktree remove --force "$wt"
fi
echo "$out"
