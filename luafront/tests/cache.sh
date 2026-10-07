#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luafront-cache.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
unset LUAFRONT_NO_CACHE
export LUAFRONT_CACHE_DIR="$tmp/cache"
export LUAFRONT_CACHE_TRACE=1

profile="$tmp/profile.syntax"
cp "$root/profiles/lua55.syntax" "$profile"

"$root/luafront" --check-profile "$profile" >"$tmp/one.out" 2>"$tmp/one.err"
grep -q 'profile cache miss' "$tmp/one.err"
grep -q 'profile cache store' "$tmp/one.err"
cachefile=$(find "$tmp/cache" -type f -name '*.lfp' | head -1)
[ -n "$cachefile" ]

"$root/luafront" --check-profile "$profile" >"$tmp/two.out" 2>"$tmp/two.err"
grep -q 'profile cache hit' "$tmp/two.err"

# Corruption is an optimization miss, never a semantic failure. The source is
# reparsed and the cache entry is repaired atomically.
printf 'corrupt-cache' >"$cachefile"
"$root/luafront" --check-profile "$profile" >"$tmp/three.out" 2>"$tmp/three.err"
grep -q 'profile cache invalid' "$tmp/three.err"
grep -q 'profile cache miss' "$tmp/three.err"
grep -q 'profile cache store' "$tmp/three.err"
"$root/luafront" --check-profile "$profile" >/dev/null 2>"$tmp/four.err"
grep -q 'profile cache hit' "$tmp/four.err"

# A stale cache format version must be rejected by the format field alone
# (the first byte after the 8-byte magic is the little-endian u32 format).
printf '\377' | dd of="$cachefile" bs=1 seek=8 conv=notrunc status=none
"$root/luafront" --check-profile "$profile" >/dev/null 2>"$tmp/fmt.err"
grep -q 'profile cache invalid' "$tmp/fmt.err"
grep -q 'profile cache miss' "$tmp/fmt.err"
grep -q 'profile cache store' "$tmp/fmt.err"
"$root/luafront" --check-profile "$profile" >/dev/null 2>"$tmp/fmt2.err"
grep -q 'profile cache hit' "$tmp/fmt2.err"

# A source change gets a different content key; exact source bytes are also
# embedded in each cache entry to make hash collisions harmless.
printf '\n# cache-key-change\n' >>"$profile"
"$root/luafront" --check-profile "$profile" >/dev/null 2>"$tmp/changed.err"
grep -q 'profile cache miss' "$tmp/changed.err"
[ "$(find "$tmp/cache" -type f -name '*.lfp' | wc -l)" -eq 2 ]

# Cache can be disabled completely for hermetic builds/tests.
rm -rf "$tmp/disabled"
LUAFRONT_CACHE_DIR="$tmp/disabled" LUAFRONT_NO_CACHE=1 \
  "$root/luafront" --check-profile "$profile" >/dev/null 2>"$tmp/disabled.err"
[ ! -e "$tmp/disabled" ]

echo 'profile cache tests passed'
