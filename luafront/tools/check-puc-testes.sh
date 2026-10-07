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
dumper='local f=assert(loadfile(os.getenv("LUAFRONT_CORPUS_FILE"))); io.stdout:write(string.dump(f,true))'
checker='assert(loadfile(os.getenv("LUAFRONT_CORPUS_FILE")))'

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
