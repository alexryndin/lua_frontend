#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

# Compile a source file through a registry profile without running it.
# builtin names (lua55) need no registration; other names are registered
# through a throwaway extension map.
lfcompile() {
  lf_c_file="$1"; lf_c_name="$2"; lf_c_path="${3:-}"
  lf_c_code="local fh=assert(io.open([==[$lf_c_file]==])); local s=fh:read('a'); fh:close(); local ok,err=require('syntax').load(s,[==[$lf_c_name]==],[==[@$lf_c_file]==]); if not ok then error(err,0) end"
  if [ -n "$lf_c_path" ]; then
    LUA_SYNTAX= LUA_SYNTAX_MAP="zz99=$lf_c_name:$(pwd)/$lf_c_path" ./lua -e "$lf_c_code"
  else
    LUA_SYNTAX= ./lua -e "$lf_c_code"
  fi
}

./lua --syntax profiles/lua55.syntax -e 'print("lua55-ok")' | grep -qx 'lua55-ok'
./lua --syntax profiles/jslike.syntax -e 'print("jslike-ok");' | grep -qx 'jslike-ok'

if ./lua --syntax tests/bad-profile.syntax -e 'print(1)' >/tmp/lf-bad.out 2>/tmp/lf-bad.err; then
  echo "bad profile unexpectedly accepted" >&2
  exit 1
fi
grep -q "undefined grammar rule 'missing'" /tmp/lf-bad.err

if ./lua --syntax tests/left-recursive.syntax -e 'print(1)' >/tmp/lf-left.out 2>/tmp/lf-left.err; then
  echo "left-recursive profile unexpectedly accepted" >&2
  exit 1
fi
grep -q "left recursion" /tmp/lf-left.err

if ./lua --syntax tests/bad-capture.syntax -e 'print(1)' >/tmp/lf-cap.out 2>/tmp/lf-cap.err; then
  echo "bad capture unexpectedly accepted" >&2
  exit 1
fi
grep -q "unknown capture 'nope'" /tmp/lf-cap.err

# Grammar acceptance through the frontend and the full PUC compile layer.
lfcompile tests/standard.lua lua55
lfcompile tests/matrix.lua lua55
./lua --syntax profiles/jslike.syntax tests/jslike.lua
./lua --syntax profiles/jslike.syntax tests/js-operators.lua

# End-to-end compiler/VM integration.  Text chunks loaded dynamically inherit
# the installed profile; binary chunks bypass the configurable syntax layer.
./lua --syntax profiles/lua55.syntax tests/standard-runtime.lua | grep -q 'standard-runtime-ok'
LUA_SYNTAX_MAP="ljs=jslike:$(pwd)/profiles/jslike.syntax" \
  ./lua --syntax profiles/jslike.syntax tests/js-runtime.lua | grep -q 'js-runtime-ok' 


# The trusted builtin lua55 instance keeps PUC's user-visible syntax
# diagnostics; other profiles surface the frontend canonical diagnostics.
# Both paths must reject invalid programs.
if ./lua tests/invalid-lvalue.lua >/tmp/lf-ilv.out 2>/tmp/lf-ilv.err; then
  echo "invalid assignment target unexpectedly accepted" >&2
  exit 1
fi
grep -q "syntax error near" /tmp/lf-ilv.err

if ./lua tests/invalid-statement.lua >/tmp/lf-ist.out 2>/tmp/lf-ist.err; then
  echo "invalid bare-name statement unexpectedly accepted" >&2
  exit 1
fi
grep -q "syntax error near" /tmp/lf-ist.err

printf 'f() = 1;\n' >/tmp/lf-ilv-js.lx
if ./lua --syntax profiles/jslike.syntax /tmp/lf-ilv-js.lx >/dev/null 2>/tmp/lf-ilv-js.err; then
  echo "jslike invalid assignment target unexpectedly accepted" >&2
  exit 1
fi
grep -q "assignment target is not a variable" /tmp/lf-ilv-js.err

printf 'bare;\n' >/tmp/lf-ist-js.lx
if ./lua --syntax profiles/jslike.syntax /tmp/lf-ist-js.lx >/dev/null 2>/tmp/lf-ist-js.err; then
  echo "jslike bare-name statement unexpectedly accepted" >&2
  exit 1
fi
grep -q "is not a valid statement" /tmp/lf-ist-js.err


# Compile and execute through the PUC Lua backend.
./lua --syntax profiles/lua55.syntax tests/runtime.lua >/tmp/lf-runtime.out
cmp tests/runtime.expected /tmp/lf-runtime.out

LUA_SYNTAX_MAP="ljs=jslike:$(pwd)/profiles/jslike.syntax" \
  ./lua --syntax profiles/jslike.syntax tests/dynamic-main.lx >/tmp/lf-dynamic.out
cmp tests/dynamic.expected /tmp/lf-dynamic.out
./lua --syntax profiles/jslike.syntax tests/loader-edge.lx | grep -qx 'loader-edge-ok'

./lua --syntax profiles/lua55.syntax tests/shebang.lua >/tmp/lf-shebang.out
grep -qx 'shebang-ok' /tmp/lf-shebang.out

# Scope-sensitive Lua semantics are rejected by the PUC compiler layer.
if lfcompile tests/invalid-break.lua lua55 >/tmp/lf-break.out 2>/tmp/lf-break.err; then
  echo "break outside loop unexpectedly compiled" >&2
  exit 1
fi
grep -q "break outside loop" /tmp/lf-break.err

# No external profile is required for standard Lua; builtin profile is the final fallback.
root=$(pwd)
(cd /tmp && "$root/lua" -e "assert(loadfile([==[$root/tests/standard.lua]==]))")


# Lua lexical semantics must survive the configurable frontend byte-for-byte,
# including embedded NUL and all short/long-string escape rules.
./lua --syntax profiles/lua55.syntax tests/strings-runtime.lua | grep -qx 'strings-runtime-ok'
if lfcompile tests/invalid-string-escape.lua lua55 >/tmp/lf-string-bad.out 2>/tmp/lf-string-bad.err; then
  echo "invalid string escape unexpectedly compiled" >&2
  exit 1
fi
grep -q 'hexadecimal digit expected' /tmp/lf-string-bad.err

# Differential runtime gate: the data-driven standard profile must preserve
# PUC behavior on multiple returns, parentheses, named varargs, short-circuit
# logic, tail calls, loops, const locals, and goto.
./lua --native tests/differential.lua >/tmp/lf-diff-native.out
./lua --syntax profiles/lua55.syntax tests/differential.lua >/tmp/lf-diff-profile.out
cmp /tmp/lf-diff-native.out /tmp/lf-diff-profile.out

# ---- configurable comment trivia ----
# Lua set: --, --[[ ]], --[=[ ]=]; comments between tokens and after
# statements; a --[ not followed by a bracket stays a line comment.
./lua --syntax profiles/lua55.syntax tests/comments-lua.lua >/tmp/lf-cmt-lua.out
cmp tests/comments-lua.expected /tmp/lf-cmt-lua.out

# JS set: //, /* */ (multiline, between tokens, trailing); comment lookalikes
# inside strings stay strings; idiv spelling maps to canonical integer
# division with native precedence.
./lua --syntax profiles/jslike.syntax tests/comments-js.lx >/tmp/lf-cmt-js.out
cmp tests/comments-js.expected /tmp/lf-cmt-js.out
./lua --native -e 'print(10 + 8 // 3 * 2)' | grep -qx '14'

# CRLF inside a block comment must not disturb following tokens.
./lua --syntax profiles/jslike.syntax tests/comments-crlf.lx | grep -qx '3'

# Unterminated comments produce normal diagnostics with correct spans.
if lfcompile tests/unterminated-block.lx jslike profiles/jslike.syntax >/tmp/lf-ubc.out 2>/tmp/lf-ubc.err; then
  echo "unterminated block comment unexpectedly compiled" >&2
  exit 1
fi
grep -q 'unfinished block comment' /tmp/lf-ubc.err
if lfcompile tests/unterminated-long.lua lua55 >/tmp/lf-ubl.out 2>/tmp/lf-ubl.err; then
  echo "unterminated long comment unexpectedly compiled" >&2
  exit 1
fi
grep -q 'unfinished long comment' /tmp/lf-ubl.err

# Multiline comments (LF and CRLF) must not shift line/column of later tokens.
if lfcompile tests/invalid-after-comment.lx jslike profiles/jslike.syntax >/tmp/lf-iac.out 2>/tmp/lf-iac.err; then
  echo "invalid fixture unexpectedly compiled" >&2
  exit 1
fi
grep -q ':4:5:' /tmp/lf-iac.err
if lfcompile tests/invalid-after-comment-crlf.lx jslike profiles/jslike.syntax >/tmp/lf-iacc.out 2>/tmp/lf-iacc.err; then
  echo "invalid CRLF fixture unexpectedly compiled" >&2
  exit 1
fi
grep -q ':4:5:' /tmp/lf-iacc.err

# Profiles without comment directives keep the built-in Lua comment set;
# declaring any comment directive replaces that set entirely.
./lua --syntax tests/legacy-nocomment.syntax tests/legacy-comment.lua
if ./lua --syntax tests/nocomment-explicit.syntax tests/legacy-comment.lua >/tmp/lf-nce.out 2>/tmp/lf-nce.err; then
  echo "'--' should not be a comment after an explicit comment directive" >&2
  exit 1
fi

# Lexical-conflict validation: exact terminal conflict, shadowed longer
# terminal, fixed-class shadowing, ambiguous opener; terminal-prefix-of-opener
# stays valid.
if ./lua --syntax tests/bad-comment-conflict.syntax -e 'print(1)' >/tmp/lf-bcc.out 2>/tmp/lf-bcc.err; then
  echo "conflicting comment profile unexpectedly accepted" >&2
  exit 1
fi
grep -q 'both a comment and a grammar terminal' /tmp/lf-bcc.err
if ./lua --syntax tests/bad-comment-shadow.syntax -e 'print(1)' >/tmp/lf-bcs.out 2>/tmp/lf-bcs.err; then
  echo "shadowing comment profile unexpectedly accepted" >&2
  exit 1
fi
grep -q 'shadows longer grammar terminal' /tmp/lf-bcs.err
if ./lua --syntax tests/bad-comment-string.syntax -e 'print(1)' >/tmp/lf-bct.out 2>/tmp/lf-bct.err; then
  echo "STRING-shadowing comment profile unexpectedly accepted" >&2
  exit 1
fi
grep -q 'shadows fixed lexical class STRING' /tmp/lf-bct.err
if ./lua --syntax tests/bad-comment-name.syntax -e 'print(1)' >/tmp/lf-bcn.out 2>/tmp/lf-bcn.err; then
  echo "NAME-shadowing comment profile unexpectedly accepted" >&2
  exit 1
fi
grep -q 'shadows fixed lexical class NAME' /tmp/lf-bcn.err
if ./lua --syntax tests/bad-comment-ambiguous.syntax -e 'print(1)' >/tmp/lf-bca.out 2>/tmp/lf-bca.err; then
  echo "ambiguous opener profile unexpectedly accepted" >&2
  exit 1
fi
grep -q 'ambiguous comment opener' /tmp/lf-bca.err
./lua --syntax tests/ok-comment-terminal-prefix.syntax -e 'return 1;' >/dev/null

# ---- per-source-unit profiles: mixed-syntax projects ----
# One process, three syntaxes: jslike main, jslike .ljs module, lua55 .lua
# module, plus an explicit syntax.load of builtin lua55.
mroot=$(pwd)
mkdir -p /tmp/lf-mixed
cp tests/jsmodule.ljs /tmp/lf-mixed/js_math.ljs
printf 'return {\n  triple = function(v) return v * 3 end,\n  quad = function(v) return v * 4 end,\n}\n' >/tmp/lf-mixed/lua_math.lua
cat >/tmp/lf-mixed/main.ljs <<'MEOF'
let js = require("js_math");
let lm = require("lua_math");
let syntax = require("syntax");
let dyn = syntax.load("return 7 + 1", "lua55");
let explicit = syntax.load("let v = 5; return v * 9;", "jslike");
print(js.value, lm.triple(7), lm.quad(2), dyn(), explicit());
MEOF
LUA_PATH="/tmp/lf-mixed/?.lua;/tmp/lf-mixed/?.ljs" \
LUA_SYNTAX_MAP="ljs=jslike:$mroot/profiles/jslike.syntax" \
  ./lua /tmp/lf-mixed/main.ljs | grep -qx '99	21	8	8	45'

# Roles come from call sites, not chunknames: require is MODULE, loadfile
# and dofile are DYNAMIC (both fall back to builtin lua55 for text), while
# the main script is ENTRY.  A fake "@...ljs" chunkname never selects jslike.
LUA_PATH="/tmp/lf-mixed/?.lua;/tmp/lf-mixed/?.ljs" \
LUA_SYNTAX_MAP="ljs=jslike:$mroot/profiles/jslike.syntax" \
LF_SYNTAX_TRACE=1 ./lua /tmp/lf-mixed/main.ljs 2>/tmp/lf-trace.err >/dev/null
grep -q 'origin=file role=entry' /tmp/lf-trace.err
grep -q 'profile=jslike reason=extension-map' /tmp/lf-trace.err
grep -q 'profile=lua55 reason=builtin-.lua' /tmp/lf-trace.err
./lua -e 'assert(load("return 1 + 1", "@/fake.ljs")())' 2>/dev/null
# loadfile resolves through the real FILE path (extension map applies to
# any FILE source); plain load() from a string never does.
LUA_SYNTAX_MAP="ljs=jslike:$mroot/profiles/jslike.syntax" \
  ./lua -e 'local f = assert(loadfile("/tmp/lf-mixed/js_math.ljs")); assert(f().value == 99)'
./lua -e 'assert(load("let x = 1;") == nil)' 2>/dev/null || \
  { echo "plain load() unexpectedly parsed jslike" >&2; exit 1; }

# syntax.load is explicit and beats any chunkname resolution.
LUA_SYNTAX_MAP="ljs=jslike:$mroot/profiles/jslike.syntax" \
  ./lua -e 'local syntax = require("syntax"); local f = assert(syntax.load("let v = 2; return v * 21;", "jslike", "@/fake.lua")); print(f())' \
  | grep -qx '42'

# Binary chunks bypass profile resolution entirely: a broken map for the
# extension must not affect loading a precompiled blob.
printf '\033Lua' >/tmp/lf-mixed/blob.ljs  # binary signature header
if LUA_SYNTAX_MAP="ljs=jslike:$mroot/tests/definitely-missing.syntax" \
   ./lua -e 'assert(loadfile("/tmp/lf-mixed/main.ljs"))' >/dev/null 2>&1; then
  :  # map is broken but this is a text file: it must FAIL below, not here
  echo "broken map unexpectedly compiled a text chunk" >&2
  exit 1
fi
./lua -e 'local blob = string.dump(function() return 5 end); local f = assert(load(blob, "@/fake.ljs")); print(f())' | grep -qx '5'

# A lazy map load failure is a strict load error naming the profile.
if LUA_SYNTAX_MAP="ljs=jslike:$mroot/tests/definitely-missing.syntax" \
   ./lua /tmp/lf-mixed/main.ljs >/tmp/lf-lazy.out 2>/tmp/lf-lazy.err; then
  echo "lazy map failure unexpectedly compiled" >&2
  exit 1
fi
grep -q "cannot load syntax profile 'jslike'" /tmp/lf-lazy.err

# Same name mapped to two different files is a deterministic error.
if LUA_SYNTAX_MAP="ljs=jslike:$mroot/profiles/jslike.syntax,jsx=jslike:$mroot/profiles/lua55.syntax" \
   ./lua -e 'print(1)' >/tmp/lf-dupname.out 2>/tmp/lf-dupname.err; then
  echo "conflicting profile-name mapping unexpectedly accepted" >&2
  exit 1
fi

./tests/cli.sh
./tools/property-differential.py
./tests/api.sh

echo "all frontend+backend tests passed"
