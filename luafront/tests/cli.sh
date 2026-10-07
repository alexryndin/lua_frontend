#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
lua="$root/lua"
lua55="$root/profiles/lua55.syntax"
js="$root/profiles/jslike.syntax"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luafront-cli.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# Version and ordinary PUC-compatible command-line execution.
"$lua" -v 2>&1 | grep -q '^Lua 5\.5\.[0-9] '
[ "$("$lua" -e 'print(6 * 7)')" = 42 ]
[ "$("$lua" --native -e 'print(6 * 7)')" = 42 ]
[ "$("$lua" --syntax "$js" -e 'print(6 * 7);')" = 42 ]
[ "$("$lua" --syntax="$js" -e 'print(40 + 2);')" = 42 ]

# Script arguments and main-chunk varargs retain stock Lua behavior.
cat >"$tmp/args.lua" <<'EOF'
print(arg[-1] ~= nil, arg[0], arg[1], arg[2], ...)
EOF
"$lua" "$tmp/args.lua" alpha beta >"$tmp/args.out"
grep -Eq '^true[[:space:]].*args\.lua[[:space:]]+alpha[[:space:]]+beta[[:space:]]+alpha[[:space:]]+beta$' "$tmp/args.out"

# '-' executes stdin; '--' terminates option processing.
printf 'print("stdin-ok")\n' | "$lua" - | grep -qx 'stdin-ok'
cat >"$tmp/-dash.lua" <<'EOF'
print("dash-ok")
EOF
(cd "$tmp" && "$lua" -- -dash.lua) | grep -qx 'dash-ok'

# The selected surface syntax applies to -e and LUA_INIT as well as files.
(
  unset LUA_INIT_5_5 LUA_INIT
  LUA_INIT='_G.initialized = 40;' \
    "$lua" --syntax "$js" -e 'print(initialized + 2);'
) | grep -qx '42'

# -E has stock Lua meaning for environment variables and also ignores
# LUA_SYNTAX, but does not disable explicit/project/system profile selection.
(
  export LUA_INIT='error("LUA_INIT must be ignored")'
  export LUA_SYNTAX='/definitely/not/a/profile.syntax'
  "$lua" -E -e 'print("noenv-ok")'
) | grep -qx 'noenv-ok'

# LUA_SYNTAX, project-local .lua-syntax, and user config search paths.
LUA_SYNTAX="$js" "$lua" -e 'print(7 * 6);' | grep -qx '42'
mkdir -p "$tmp/project"
cp "$js" "$tmp/project/.lua-syntax"
(cd "$tmp/project" && "$lua" -e 'print(7 * 6);') | grep -qx '42'
mkdir -p "$tmp/home/.config/lua" "$tmp/home-work"
cp "$js" "$tmp/home/.config/lua/syntax"
(cd "$tmp/home-work" && HOME="$tmp/home" "$lua" -e 'print(7 * 6);') | grep -qx '42'

# Selection precedence: explicit > LUA_SYNTAX > project > user > builtin.
LUA_SYNTAX="$lua55" "$lua" --syntax "$js" -e 'print(7 * 6);' | grep -qx '42'
(cd "$tmp/project" && HOME="$tmp/home" LUA_SYNTAX="$lua55" "$lua" -e 'print(42)') | grep -qx '42'
(cd "$tmp/home-work" && HOME="$tmp/home" "$lua" --native -e 'print(42)') | grep -qx '42'

# A bad explicit/environment selection is strict and never silently falls back.
if "$lua" --syntax "$tmp/missing.syntax" -e 'print(1)' >"$tmp/missing.out" 2>"$tmp/missing.err"; then
  echo 'missing explicit syntax profile unexpectedly accepted' >&2
  exit 1
fi
grep -q 'cannot open profile' "$tmp/missing.err"
if LUA_SYNTAX="$tmp/missing.syntax" "$lua" -e 'print(1)' >"$tmp/envmissing.out" 2>"$tmp/envmissing.err"; then
  echo 'missing LUA_SYNTAX profile unexpectedly accepted' >&2
  exit 1
fi
grep -q 'cannot open profile' "$tmp/envmissing.err"

# --native and --syntax are intentionally mutually exclusive.
if "$lua" --native --syntax "$js" -e 'print(1)' >"$tmp/conflict.out" 2>"$tmp/conflict.err"; then
  echo '--native/--syntax conflict unexpectedly accepted' >&2
  exit 1
fi
grep -q 'usage:' "$tmp/conflict.err"

# -l uses require and therefore inherits the active profile for Lua modules.
LUA_PATH="$root/tests/?.lua;;" \
  "$lua" --syntax "$js" -l M=jsmodule -e 'print(M.value);' | grep -qx '99'

# Profile-aware REPL: expression shorthand, declarations, calls, multiline
# blocks, and EOF while incomplete all use the selected grammar.
printf '1 + 2\nfunction twice(x) {\nreturn x * 2;\n}\ntwice(4)\n' \
  | "$lua" --syntax "$js" -i >"$tmp/repl.out" 2>"$tmp/repl.err"
grep -q '3' "$tmp/repl.out"
grep -q '8' "$tmp/repl.out"
[ ! -s "$tmp/repl.err" ]

printf 'function unfinished(x) {\n' \
  | "$lua" --syntax "$js" -i >"$tmp/incomplete.out" 2>"$tmp/incomplete.err"
grep -q "expected } near '<eof>'" "$tmp/incomplete.err"
if grep -q 'error message not a string' "$tmp/incomplete.err"; then
  echo 'REPL lost its syntax error object' >&2
  exit 1
fi

# Native mode bypasses the configurable grammar.
if printf 'let x = 1;\n' | "$lua" --native - >"$tmp/native-js.out" 2>"$tmp/native-js.err"; then
  echo 'native PUC parser unexpectedly accepted JS-like syntax' >&2
  exit 1
fi

echo 'lua CLI tests passed'
