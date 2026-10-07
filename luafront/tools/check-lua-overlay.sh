#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luafront-overlay.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# The repository keeps an upstream-pristine lua.c; the CLI build applies
# this patch to a generated copy.  The gate verifies the overlay still
# applies cleanly to the current pristine source and that every expected
# role marker lands in the result.
cp "$root/../lua.c" "$tmp/lua.c"
patch -s -N -p1 -d "$tmp" < "$root/patches/lua-cli-sourceinfo.patch"

markers=$(grep -c 'LUA_ROLE_' "$tmp/lua.c")
if [ "$markers" -ne 7 ]; then
  echo "unexpected role marker count: $markers (expected 7)" >&2
  exit 1
fi
for m in LUA_ROLE_ENTRY LUA_ROLE_LUAINIT luaL_loadfilex_source luaL_loadbufferx_source; do
  grep -q "$m" "$tmp/lua.c" || { echo "marker missing: $m" >&2; exit 1; }
done

echo "lua.c overlay verified (7 role markers)"
