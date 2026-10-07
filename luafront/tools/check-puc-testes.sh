#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 /path/to/lua-5.5/testes" >&2
  exit 2
fi

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
testes=$1
profile="$root/profiles/lua55.syntax"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luafront-corpus.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

if [ ! -d "$testes" ]; then
  echo "not a directory: $testes" >&2
  exit 2
fi

# Stripping debug information intentionally focuses this gate on compiler
# semantics/code generation. Source-line parity is tested separately by the
# diagnostics/runtime suite, while custom syntax naturally has different text
# positions from standard Lua.
#
# The native side uses loadfile with the compiler hook off.  The profile
# side compiles the same source through an explicit registry profile:
# --syntax selects the entry override, whose declared name is then used
# with syntax.load so every chunk goes through the frontend deterministically.
name=$(sed -n 's/^profile[[:space:]][[:space:]]*\([A-Za-z0-9_-]*\);.*/\1/p' "$profile" | head -1)
[ -n "$name" ] || { echo "cannot determine profile name of $profile" >&2; exit 2; }

dumper="local p=os.getenv(\"LUAFRONT_CORPUS_FILE\"); local fh=assert(io.open(p)); local s=fh:read('a'); fh:close(); if s:sub(1,1)=='#' then s=s:match('^#[^\n]*\n(.*)') or '' end; local fn=assert(require('syntax').load(s, \"$name\", \"@\"..p)); io.stdout:write(string.dump(fn,true))"
checker="local p=os.getenv(\"LUAFRONT_CORPUS_FILE\"); local fh=assert(io.open(p)); local s=fh:read('a'); fh:close(); if s:sub(1,1)=='#' then s=s:match('^#[^\n]*\n(.*)') or '' end; assert(require('syntax').load(s, \"$name\", \"@\"..p))"

ok=0
for f in "$testes"/*.lua; do
  [ -f "$f" ] || continue
  base=$(basename "$f")

  LUAFRONT_CORPUS_FILE="$f" "$root/lua" --native -e "$checker" >/dev/null
  LUAFRONT_CORPUS_FILE="$f" "$root/lua" --syntax "$profile" -e "$checker" >/dev/null

  LUAFRONT_CORPUS_FILE="$f" "$root/lua" --native -e "$dumper" \
    >"$tmp/native.luac"
  LUAFRONT_CORPUS_FILE="$f" "$root/lua" --syntax "$profile" -e "$dumper" \
    >"$tmp/profile.luac"
  if ! cmp -s "$tmp/native.luac" "$tmp/profile.luac"; then
    echo "BYTECODE-DIFF  $base" >&2
    exit 1
  fi

  printf 'OK  %s\n' "$base"
  ok=$((ok + 1))
done

printf 'native/profile compile + stripped-bytecode identical: %d files\n' "$ok"
