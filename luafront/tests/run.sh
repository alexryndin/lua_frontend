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

# ---- configurable comment trivia ----
# Lua set: --, --[[ ]], --[=[ ]=]; comments between tokens and after
# statements; a --[ not followed by a bracket stays a line comment.
./luax --syntax profiles/lua55.syntax tests/comments-lua.lua >/tmp/lf-cmt-lua.out
cmp tests/comments-lua.expected /tmp/lf-cmt-lua.out

# JS set: //, /* */ (multiline, between tokens, trailing); comment lookalikes
# inside strings stay strings; idiv spelling maps to canonical integer
# division with native precedence.
./luax --syntax profiles/jslike.syntax tests/comments-js.lx >/tmp/lf-cmt-js.out
cmp tests/comments-js.expected /tmp/lf-cmt-js.out
./lua --native -e 'print(10 + 8 // 3 * 2)' | grep -qx '14'

# CRLF inside a block comment must not disturb following tokens.
./luax --syntax profiles/jslike.syntax tests/comments-crlf.lx | grep -qx '3'

# Unterminated comments produce normal diagnostics with correct spans.
if ./luax --check --syntax profiles/jslike.syntax tests/unterminated-block.lx >/tmp/lf-ubc.out 2>/tmp/lf-ubc.err; then
  echo "unterminated block comment unexpectedly compiled" >&2
  exit 1
fi
grep -q 'unfinished block comment' /tmp/lf-ubc.err
if ./luax --check --syntax profiles/lua55.syntax tests/unterminated-long.lua >/tmp/lf-ubl.out 2>/tmp/lf-ubl.err; then
  echo "unterminated long comment unexpectedly compiled" >&2
  exit 1
fi
grep -q 'unfinished long comment' /tmp/lf-ubl.err

# Multiline comments (LF and CRLF) must not shift line/column of later tokens.
if ./luax --check --syntax profiles/jslike.syntax tests/invalid-after-comment.lx >/tmp/lf-iac.out 2>/tmp/lf-iac.err; then
  echo "invalid fixture unexpectedly compiled" >&2
  exit 1
fi
grep -q ':4:5:' /tmp/lf-iac.err
if ./luax --check --syntax profiles/jslike.syntax tests/invalid-after-comment-crlf.lx >/tmp/lf-iacc.out 2>/tmp/lf-iacc.err; then
  echo "invalid CRLF fixture unexpectedly compiled" >&2
  exit 1
fi
grep -q ':4:5:' /tmp/lf-iacc.err

# Profiles without comment directives keep the built-in Lua comment set;
# declaring any comment directive replaces that set entirely.
./luax --check --syntax tests/legacy-nocomment.syntax tests/legacy-comment.lua
if ./luax --check --syntax tests/nocomment-explicit.syntax tests/legacy-comment.lua >/tmp/lf-nce.out 2>/tmp/lf-nce.err; then
  echo "'--' should not be a comment after an explicit comment directive" >&2
  exit 1
fi

# Lexical-conflict validation: exact terminal conflict, shadowed longer
# terminal, fixed-class shadowing, ambiguous opener; terminal-prefix-of-opener
# stays valid.
if ./luafront --check-profile tests/bad-comment-conflict.syntax >/tmp/lf-bcc.out 2>/tmp/lf-bcc.err; then
  echo "conflicting comment profile unexpectedly accepted" >&2
  exit 1
fi
grep -q 'both a comment and a grammar terminal' /tmp/lf-bcc.err
if ./luafront --check-profile tests/bad-comment-shadow.syntax >/tmp/lf-bcs.out 2>/tmp/lf-bcs.err; then
  echo "shadowing comment profile unexpectedly accepted" >&2
  exit 1
fi
grep -q 'shadows longer grammar terminal' /tmp/lf-bcs.err
if ./luafront --check-profile tests/bad-comment-string.syntax >/tmp/lf-bct.out 2>/tmp/lf-bct.err; then
  echo "STRING-shadowing comment profile unexpectedly accepted" >&2
  exit 1
fi
grep -q 'shadows fixed lexical class STRING' /tmp/lf-bct.err
if ./luafront --check-profile tests/bad-comment-name.syntax >/tmp/lf-bcn.out 2>/tmp/lf-bcn.err; then
  echo "NAME-shadowing comment profile unexpectedly accepted" >&2
  exit 1
fi
grep -q 'shadows fixed lexical class NAME' /tmp/lf-bcn.err
if ./luafront --check-profile tests/bad-comment-ambiguous.syntax >/tmp/lf-bca.out 2>/tmp/lf-bca.err; then
  echo "ambiguous opener profile unexpectedly accepted" >&2
  exit 1
fi
grep -q 'ambiguous comment opener' /tmp/lf-bca.err
./luafront --check-profile tests/ok-comment-terminal-prefix.syntax >/dev/null

./tests/cli.sh
./tests/cache.sh
./tools/property-differential.py
./tests/api.sh

echo "all frontend+backend tests passed"
