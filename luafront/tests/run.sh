#!/bin/sh
set -eu
# Keep the main gate hermetic; cache behavior has its own dedicated test.
export LUAFRONT_NO_CACHE=1
cd "$(dirname "$0")/.."

./luafront --check-profile profiles/lua55.syntax | grep -q 'profile lua55: OK'
./luafront --check-profile profiles/jslike.syntax | grep -q 'profile jslike: OK'

if ./luafront --check-profile tests/bad-profile.syntax >/tmp/lf-bad.out 2>/tmp/lf-bad.err; then
  echo "bad profile unexpectedly accepted" >&2
  exit 1
fi
grep -q "undefined grammar rule 'missing'" /tmp/lf-bad.err

if ./luafront --check-profile tests/left-recursive.syntax >/tmp/lf-left.out 2>/tmp/lf-left.err; then
  echo "left-recursive profile unexpectedly accepted" >&2
  exit 1
fi
grep -q "left recursion" /tmp/lf-left.err

if ./luafront --check-profile tests/bad-capture.syntax >/tmp/lf-cap.out 2>/tmp/lf-cap.err; then
  echo "bad capture unexpectedly accepted" >&2
  exit 1
fi
grep -q "unknown capture 'nope'" /tmp/lf-cap.err

./luafront --syntax profiles/lua55.syntax tests/standard.lua >/tmp/lf-standard.ast
./luafront --syntax profiles/lua55.syntax tests/matrix.lua >/tmp/lf-matrix.ast
./luafront --syntax profiles/jslike.syntax tests/jslike.lua >/tmp/lf-js.ast
./luafront --syntax profiles/jslike.syntax tests/js-operators.lua >/tmp/lf-jsops.ast

grep -q 'FunctionDecl' /tmp/lf-standard.ast
grep -q 'NumericFor' /tmp/lf-matrix.ast
grep -q 'GenericFor' /tmp/lf-matrix.ast
grep -q 'Vararg' /tmp/lf-matrix.ast
grep -q 'Binary("and")' /tmp/lf-js.ast
grep -q 'Binary("~=")' /tmp/lf-js.ast
grep -q 'Unary("not")' /tmp/lf-jsops.ast

# End-to-end compiler/VM integration.  Text chunks loaded dynamically inherit
# the installed profile; binary chunks bypass the configurable syntax layer.
./luax --syntax profiles/lua55.syntax tests/standard-runtime.lua | grep -q 'standard-runtime-ok'
./luax --syntax profiles/jslike.syntax tests/js-runtime.lua | grep -q 'js-runtime-ok'


if ./luafront --syntax profiles/lua55.syntax tests/invalid-lvalue.lua >/tmp/lf-ilv.out 2>/tmp/lf-ilv.err; then
  echo "invalid assignment target unexpectedly accepted" >&2
  exit 1
fi
grep -q "assignment target is not a variable" /tmp/lf-ilv.err

if ./luafront --syntax profiles/lua55.syntax tests/invalid-statement.lua >/tmp/lf-ist.out 2>/tmp/lf-ist.err; then
  echo "invalid bare-name statement unexpectedly accepted" >&2
  exit 1
fi
grep -q "is not a valid statement" /tmp/lf-ist.err


# Compile and execute through the PUC Lua backend.
./luax --syntax profiles/lua55.syntax tests/runtime.lua >/tmp/lf-runtime.out
cmp tests/runtime.expected /tmp/lf-runtime.out

./luax --syntax profiles/jslike.syntax tests/dynamic-main.lx >/tmp/lf-dynamic.out
cmp tests/dynamic.expected /tmp/lf-dynamic.out
./luax --syntax profiles/jslike.syntax tests/loader-edge.lx | grep -qx 'loader-edge-ok'

./luax --syntax profiles/lua55.syntax tests/shebang.lua >/tmp/lf-shebang.out
grep -qx 'shebang-ok' /tmp/lf-shebang.out

# Scope-sensitive Lua semantics are rejected by the PUC compiler layer.
if ./luax --check --syntax profiles/lua55.syntax tests/invalid-break.lua >/tmp/lf-break.out 2>/tmp/lf-break.err; then
  echo "break outside loop unexpectedly compiled" >&2
  exit 1
fi
grep -q "break outside loop" /tmp/lf-break.err

# No external profile is required for standard Lua; builtin profile is the final fallback.
root=$(pwd)
(cd /tmp && "$root/luax" --check "$root/tests/standard.lua" >/dev/null)


# Lua lexical semantics must survive the configurable frontend byte-for-byte,
# including embedded NUL and all short/long-string escape rules.
./luax --syntax profiles/lua55.syntax tests/strings-runtime.lua | grep -qx 'strings-runtime-ok'
./luafront --syntax profiles/lua55.syntax tests/strings-runtime.lua | grep -q 'String("a\\0b")'
if ./luax --check --syntax profiles/lua55.syntax tests/invalid-string-escape.lua >/tmp/lf-string-bad.out 2>/tmp/lf-string-bad.err; then
  echo "invalid string escape unexpectedly compiled" >&2
  exit 1
fi
grep -q 'hexadecimal digit expected' /tmp/lf-string-bad.err

# Differential runtime gate: the data-driven standard profile must preserve
# PUC behavior on multiple returns, parentheses, named varargs, short-circuit
# logic, tail calls, loops, const locals, and goto.
./luax --native tests/differential.lua >/tmp/lf-diff-native.out
./luax --syntax profiles/lua55.syntax tests/differential.lua >/tmp/lf-diff-profile.out
cmp /tmp/lf-diff-native.out /tmp/lf-diff-profile.out

./tests/cli.sh
./tests/cache.sh
./tools/property-differential.py
./tests/api.sh

echo "all frontend+backend tests passed"
