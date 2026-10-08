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

# -e is an ENTRY unit and uses the selected syntax; LUA_INIT keeps its own
# resolution (Lua by default, extension map for @files) either way.
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

# require resolves each module by its own file extension, never by the
# dialect of the importing unit: .ljs through the map, .lua stays Lua.
mkdir -p "$tmp/mods"
cp "$root/tests/jsmodule.ljs" "$tmp/mods/jsmodule.ljs"
printf 'return {value = 7}\n' >"$tmp/mods/lmod.lua"
LUA_PATH="$tmp/mods/?.lua;$tmp/mods/?.ljs" LUA_SYNTAX_MAP="ljs=jslike:$js" \
  "$lua" --syntax "$js" -e 'let a = require("jsmodule"); let b = require("lmod"); print(a.value + b.value);' \
  | grep -qx '106'

# Extension maps apply to any FILE source unit, including LUA_INIT @files.
printf 'print("init-js", 2 * 21);\n' >"$tmp/mods/init.ljs"
(
  unset LUA_INIT_5_5 LUA_INIT
  LUA_INIT="@$tmp/mods/init.ljs" LUA_SYNTAX_MAP="ljs=jslike:$js" \
    "$lua" -e 'print("main-ok")'
) | grep -qx 'init-js	42
main-ok'

# -E ignores environment-driven syntax configuration, LUA_SYNTAX_MAP included.
if LUA_SYNTAX_MAP="ljs=jslike:$js" LUA_INIT='error("LUA_INIT must be ignored")' \
   "$lua" -E -e 'return 1' 2>"$tmp/e-map.err" >/dev/null; then
  :  # -E succeeded; the map must NOT have registered, nothing to check here
else
  echo '-E unexpectedly failed' >&2
  exit 1
fi
printf 'let x = 1;\n' >"$tmp/mods/plain.ljs"
if LUA_SYNTAX_MAP="ljs=jslike:$js" "$lua" -E "$tmp/mods/plain.ljs" \
    >"$tmp/e-map.out" 2>"$tmp/e-map.err"; then
  echo '-E unexpectedly applied LUA_SYNTAX_MAP' >&2
  exit 1
fi

# --syntax-map CLI entries override env entries for the same extension.
printf 'let y = 2;\n' >"$tmp/mods/cli.ljs"
LUA_SYNTAX_MAP="ljs=jslike:$tmp/definitely-missing.syntax" \
  "$lua" --syntax-map "ljs=jslike:$js" "$tmp/mods/cli.ljs"

# Reserved names and duplicate extensions are configuration errors.
if "$lua" --syntax-map "lua=jslike:$js" -e 'print(1)' \
    >"$tmp/res-ext.out" 2>"$tmp/res-ext.err"; then
  echo "reserved extension 'lua' unexpectedly accepted" >&2
  exit 1
fi
grep -q "reserved" "$tmp/res-ext.err"
if "$lua" --syntax-map "ljs=lua55:$js" -e 'print(1)' \
    >"$tmp/res-name.out" 2>"$tmp/res-name.err"; then
  echo "reserved name 'lua55' unexpectedly accepted" >&2
  exit 1
fi
if LUA_SYNTAX_MAP="ljs=jslike:$js,ljs=jslike:$js" "$lua" -e 'print(1)' \
    >"$tmp/dup.out" 2>"$tmp/dup.err"; then
  echo "duplicate extension mapping unexpectedly accepted" >&2
  exit 1
fi

# Profile-aware REPL: expression shorthand, declarations, calls, multiline
# blocks, and EOF while incomplete all use the selected grammar.
printf '1 + 2\nfunction twice(x) {\nreturn x * 2;\n}\ntwice(4)\n' \
  | "$lua" --syntax "$js" -i >"$tmp/repl.out" 2>"$tmp/repl.err"
grep -q '3' "$tmp/repl.out"
grep -q '8' "$tmp/repl.out"
[ ! -s "$tmp/repl.err" ]

# The PUC REPL itself remains unmodified: expression shorthand is supplied by
# the compiler hook even for a profile that has no surface `return` keyword.
printf '1 + 2\n' \
  | "$lua" --syntax "$root/tests/repl-expression.syntax" -i \
      >"$tmp/repl-noreturn.out" 2>"$tmp/repl-noreturn.err"
grep -q '3' "$tmp/repl-noreturn.out"
[ ! -s "$tmp/repl-noreturn.err" ]

# Line comments are ordinary trivia in the REPL as well.
printf '1 + 2 // repl note\n' \
  | "$lua" --syntax "$js" -i >"$tmp/repl-comment.out" 2>"$tmp/repl-comment.err"
grep -q '3' "$tmp/repl-comment.out"
[ ! -s "$tmp/repl-comment.err" ]

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
