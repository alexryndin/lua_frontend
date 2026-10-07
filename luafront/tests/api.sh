#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luafront-api.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc=${CC:-cc}
api_sanitize_flags=${API_SANITIZE_FLAGS:-}

"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/include" \
  "$root/examples/parse.c" "$root/libluafront.a" $api_sanitize_flags -o "$tmp/parse"
"$tmp/parse" | grep -qx 'profile=lua55 version=5.5.0 root=Chunk'

"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/include" -I"$root/.." \
  "$root/examples/embed.c" "$root/libluafront-lua.a" $api_sanitize_flags -lm -ldl -o "$tmp/embed"
"$tmp/embed" "$root/profiles/jslike.syntax" | grep -qx '42'

echo 'public C API tests passed'
