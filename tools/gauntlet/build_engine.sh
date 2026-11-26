#!/usr/bin/env bash
# Build a reference engine into tools/engines/<engine>-<version>.
#   tools/gauntlet/build_engine.sh <engine> <version>
# Versions and git tags come from tools/gauntlet/ccrl_blitz.csv.
set -euo pipefail
root="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$root"
name=${1:?engine}; ver=${2:?version}
out="tools/engines/$name-$ver"
[ -x "$out" ] && { echo "$out"; exit 0; }
tag=$(awk -F, -v n="$name" -v v="$ver" '$1==n && $2==v {print $3}' tools/gauntlet/ccrl_blitz.csv)
[ -n "$tag" ] || { echo "no git tag for $name $ver" >&2; exit 1; }
src="tools/engines/src/$name-$ver"
mkdir -p tools/engines/src
case "$name" in
  stash) repo=https://github.com/mhouppin/stash-bot.git ;;
  weiss) repo=https://github.com/TerjeKir/weiss.git ;;
  *) echo "don't know how to fetch $name" >&2; exit 1 ;;
esac
[ -d "$src" ] || git clone -q --depth 1 --branch "$tag" "$repo" "$src"
jobs=${BUILD_JOBS:-4}
# Makefile location differs between versions (repo root or src/).
mk="$src"; [ -f "$src/src/Makefile" ] && mk="$src/src"
case "$name" in
  stash)
    # Newer Stash accepts ARCH=native; older versions ignore unknown ARCH values.
    make -C "$mk" -j"$jobs" ARCH=native >/dev/null 2>&1 || make -C "$mk" -j"$jobs" >/dev/null ;;
  weiss)
    make -C "$mk" -j"$jobs" >/dev/null ;;
esac
# Newest executable regular file named after the engine.
bin=$(find "$src" -maxdepth 2 -type f -perm -u+x -iname "$name*" ! -name "*.sh" -print0 | xargs -0 ls -t 2>/dev/null | head -1)
[ -x "$bin" ] || { echo "build of $name $ver produced no binary" >&2; exit 1; }
cp "$bin" "$out"
echo "$out"
