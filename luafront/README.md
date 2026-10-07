# Configurable Lua Frontend

A data-driven syntax frontend for **Lua 5.5**, with **PUC Lua** (this
repository) as the compiler/runtime baseline.  The frontend lives in
`luafront/`; the Lua sources at the repository root carry the small
integration patch described in
[docs/PUC_INTEGRATION.md](docs/PUC_INTEGRATION.md).

The project changes Lua's **surface syntax only**.  Keywords, operators,
delimiters, block notation and grammar structure are described by an external
EBNF-like profile.  A profile maps matched syntax into a fixed canonical Lua
AST; that AST is lowered to typed canonical Lua tokens and fed into the normal
PUC parser/code generator.  Lua values, scope rules, closures, multiple
returns, varargs, goto rules, `<const>/<close>`, coroutines, bytecode and VM
semantics are therefore not configurable.

Release status: **1.0.1 / feature-complete for the stated design**.

## Quick start

```sh
make -C luafront
make -C luafront test
```

Run ordinary Lua through the bundled data-driven Lua 5.5 profile:

```sh
./lua program.lua
./lua -e 'print(40 + 2)'
```

(from the `luafront/` directory)

Run an alternate syntax:

```sh
./lua --syntax profiles/jslike.syntax program.lx
```

For example, this is still Lua semantically:

```text
let x = 10;
let y = 20;

function max(a, b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

print(max(x, y));
```

The JS-like profile maps `let`, braces, `&&`, `||`, `!`, `!=`, `null`, and
other surface forms into canonical Lua constructs.  It uses JS-style comments
(`//` and `/* ... */`); since `//` is a comment there, integer division is
written as the word operator `idiv` (mapping to the canonical Lua `//`).  No
JavaScript runtime semantics are introduced.

## `lua` command line

The primary executable uses the **unmodified upstream PUC Lua `lua.c`** (the
repository root file, guarded byte-for-byte by `make check-upstream`).
A small launcher removes the two luafront-specific options, selects the profile,
installs the source-compiler hook when PUC creates its `lua_State`, and then
hands control to the stock PUC CLI.  This keeps option parsing, `arg`, signal
handling, `LUA_INIT`, `-e`, `-l`, stdin handling and the REPL in upstream code.

The launcher adds two options:

```text
--syntax FILE    select a syntax profile explicitly
--native         bypass the configurable frontend and use the native PUC parser
```

The normal Lua options remain available:

```text
-e stat
-i
-l mod
-l name=mod
-v
-E
-W
--
-
```

The selected profile applies to ENTRY source units only:

- the main script;
- `-e` chunks;
- interactive REPL input;
- stdin.

`LUA_INIT`, `load`, `loadfile`, `dofile` and `require`d modules resolve
their own profiles (see [Per-source-unit profiles](#per-source-unit-profiles)):
`.lua` files and string `load()` behave exactly like stock Lua 5.5.

Binary chunks always bypass the configurable source frontend and go directly
to PUC's binary undumper.  `load(..., "b")` / `load(..., "t")` mode checks and
custom `load` readers preserve PUC behavior.

The REPL is still PUC's normal REPL.  The compiler hook recognizes PUC's
expression probe at the load boundary and delegates the original expression to
the profile's optional `repl_expr` rule.  Incomplete frontend input is reported
with PUC's `<eof>` convention, so upstream multiline REPL handling remains
unchanged and no surface-language keyword is hard-coded into `lua.c`.

## Per-source-unit profiles

The syntax profile is a property of each source unit, not of the Lua state.
One process can mix dialects freely:

```text
main.ljs    -- jslike, via extension map
foo.lua     -- lua55 (builtin), required from main.ljs
bar.ljs     -- jslike module
vendor.lua  -- lua55
        ↓  all become ordinary Lua closures / bytecode in one Lua VM
```

`require`, function calls, tables, closures, coroutines and the C API know
nothing about syntax profiles.  After compilation a profile stops existing
as a meaningful entity; binary chunks bypass profile resolution entirely.

Each text load resolves a profile deterministically:

1. explicit — `syntax.load(source, name)` / `lf_lua_loadsyntax`;
2. entry override (`--syntax`, `LUA_SYNTAX`, `.lua-syntax`, user/system
   config) — applies only to ENTRY units: the main script, `-e` chunks,
   stdin and the REPL;
3. extension map, for real FILE paths only (`loadfile`, `dofile`,
   `require`);
4. `*.lua` files use builtin lua55;
5. everything else (string `load`, custom readers) uses builtin lua55.

Neither chunknames nor profile-file basenames participate: `load(src,
"@/x/f.ljs")` is a MEMORY source and compiles as Lua 5.5.  `LUA_INIT`
never inherits the entry override, but FILE inits do go through the
extension map.  Roles come from the call sites themselves, carried in
`lua_SourceInfo` (see `docs/PUC_INTEGRATION.md`).

### Extension maps

```sh
export LUA_PATH="./?.lua;./?.ljs"
export LUA_SYNTAX_MAP="ljs=jslike:/path/jslike.syntax,moon=moon:/path/moon.syntax"
lua main.ljs
```

- format: `ext=name:path`; the file must declare `profile name;` matching
  the map (names are declared, never derived from basenames);
- `lua --syntax-map ljs=jslike:/path/jslike.syntax` overrides env entries
  per extension; `-E` ignores `LUA_SYNTAX_MAP`;
- profiles load lazily on first use; a broken map entry is a strict load
  error;
- `lua55` and the `lua` extension are reserved for the builtin; the same
  name may map several extensions only when the path is identical;
- `LF_SYNTAX_TRACE=1` prints origin/role/profile/reason for every text
  load.

### The `syntax` library

```lua
local syntax = require("syntax")
local f = syntax.load(src, "jslike")               -- like load, explicit profile
local f = syntax.load(src, "jslike", "=gen", "t", env)
local lua_f = syntax.load(src, "lua55")            -- builtin instance
```

`load()` itself is unchanged: `load(src)` always uses builtin lua55.

## Profile loading

External syntax files are parsed into grammar IR on every use.  Realistic
profiles are a few kilobytes and parse in well under a millisecond, so there
is deliberately no on-disk compiled-profile cache.

## Syntax profile

A minimal profile looks like this:

```text
profile tiny;
version "1.0";

rule chunk = block:body EOF => Chunk(body);
rule block = { stat:items } => Block(items);

rule stat = local_decl | return_stat;
rule local_decl = "let" NAME:name "=" exp:value ";"
                  => Local(DeclList(DeclName(name)), ExprList(value));
rule return_stat = "return" exp:value ";" => Return(ExprList(value));

rule exp = add_expr:value => Pass(value);
rule add_expr = primary:first { add_tail:rest } => Fold(first, rest);
rule add_tail = "+":op primary:value => Tail(op, value);
rule primary = NAME:value => Pass(value) | NUMBER:value => Pass(value);
```

`NAME`, `NUMBER`, `STRING`, and `EOF` are fixed lexical classes, as are
whitespace and Lua short/long strings.  Comments are configurable trivia via
`line_comment` / `block_comment` / `lua_long_comment` directives (skipped by
the lexer, never visible to the grammar); without directives the built-in Lua
comment set is used.  Keywords, operators, punctuation and delimiters come
from string terminals in the profile and use longest-match tokenization.

The profile language contains no executable callbacks.  Semantic mappings may
only construct the fixed canonical Lua AST or use fixed mapping helpers such as
`Pass`, `Fold`, `Right`, and `Postfix`.

See [docs/PROFILE_FORMAT.md](docs/PROFILE_FORMAT.md).

## Architecture

```text
syntax profile
     |
     v
profile parser -> grammar IR -> validator
                                      |
source -> configurable lexer -> generic parser
                                      |
                                      v
                               canonical Lua AST
                                      |
                                      v
                            canonical Lua tokens
                                      |
                    +-----------------+-----------------+
                    |                                   |
                    |                         native Lua lexer
                    |                                   |
                    +-----------------+-----------------+
                                      |
                                      v
                                PUC lparser.c
                                      |
                                      v
                                 PUC lcode.c
                                      |
                                      v
                                bytecode / VM
```

There is **no generated intermediate Lua source**.  Source spans on canonical
nodes/tokens refer to the original dialect file.

The PUC changes are deliberately limited to a token-reader entry point and a
per-state source-compiler hook.  See
[docs/PUC_INTEGRATION.md](docs/PUC_INTEGRATION.md).

## Tools

`lua` is the interpreter.  Compile-only checking is plain `loadfile`
(the hook applies to it like to any other text chunk):

```sh
./lua --native file.lua
./lua --syntax profiles/lua55.syntax file.lua
./lua --syntax profiles/jslike.syntax -e 'assert(loadfile("file.lx"));'
```

## Library APIs

Two static libraries are built:

```text
libluafront.a       profile/parser/canonical-AST API
libluafront-lua.a   frontend + PUC Lua compiler/runtime bridge
```

Public headers:

```text
include/luafront.h
include/luafront_lua.h
```

Minimal embedding examples:

```c
/* parse — load a profile and parse a chunk to a canonical AST
   (compile: cc -Iinclude examples-native.c libluafront.a) */
#include "luafront.h"

#include <stdio.h>

int main(void) {
    static const char source[] = "return 40 + 2";
    LF_Error err = {0};
    LF_Profile *profile = lf_profile_builtin_lua55(&err);
    if (profile == NULL) {
        fprintf(stderr, "%s\n", err.message);
        return 1;
    }
    LF_Ast *ast = lf_parse(profile, "=example", source,
                           sizeof(source) - 1, &err);
    if (ast == NULL) {
        fprintf(stderr, "%s\n", err.message);
        lf_profile_free(profile);
        return 1;
    }
    printf("profile=%s version=%s root=%s\n",
           lf_profile_name(profile),
           lf_profile_version(profile) ? lf_profile_version(profile) : "-",
           lf_ast_kind(ast));
    lf_ast_free(ast);
    lf_profile_free(profile);
    return 0;
}
```

```c
/* embed — run a dialect chunk on a PUC Lua state with a profile installed
   (compile: cc -Iinclude -I.. embed-native.c libluafront-lua.a -lm -ldl) */
#include "luafront_lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    static const char source[] = "let answer = 40 + 2; return answer;";
    LF_Error err = {0};
    if (argc != 2) {
        fprintf(stderr, "usage: %s PROFILE\n", argv[0]);
        return 2;
    }
    LF_Profile *profile = lf_profile_load(argv[1], &err);
    if (profile == NULL) {
        fprintf(stderr, "%s\n", err.message);
        return 1;
    }
    lua_State *L = luaL_newstate();
    if (L == NULL) return 1;
    luaL_openlibs(L);
    lf_lua_install_profile(L, profile);
    if (luaL_loadbuffer(L, source, sizeof(source) - 1, "=embedded") != LUA_OK ||
        lua_pcall(L, 0, 1, 0) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lf_lua_uninstall_profile(L);
        lf_profile_free(profile);
        lua_close(L);
        return 1;
    }
    printf("%lld\n", (long long)lua_tointeger(L, -1));
    lf_lua_uninstall_profile(L);
    lf_profile_free(profile);
    lua_close(L);
    return 0;
}
```

## Install

```sh
make install PREFIX=/usr/local
```

`DESTDIR` is supported for packaging:

```sh
make install DESTDIR=/tmp/pkg PREFIX=/usr
```

This installs executables, both static libraries, frontend/Lua public headers,
and the reference syntax profiles.  The default Lua profile is also embedded
in the binaries, so an installed interpreter does not depend on the external
profile file.

## Verification

Normal release gate:

```sh
make test
```

Strict compiler gate:

```sh
make strict
```

ASan + UBSan gate:

```sh
make sanitize
```

Official Lua 5.5 syntax/compiler differential gate:

```sh
make corpus TESTES=/path/to/lua-5.5/testes
```

On the available official Lua 5.5 corpus, all **35/35** `testes/*.lua` files:

- compile through the native PUC parser;
- compile through `lua55.syntax` and the configurable frontend;
- produce **byte-for-byte identical stripped bytecode** in both paths.

The regular test gate additionally covers the upstream CLI integration, REPL, profile search,
binary-safe strings, all Lua short-string
escape forms, dynamic loading, binary chunks, public C APIs, and a
deterministic generated Lua-vs-JS-like differential test over 180 expressions.

See [docs/VERIFICATION-1.0.0.md](docs/VERIFICATION-1.0.0.md).

## Scope / non-goals

The current baseline is **Lua 5.5.1** (the repository root); moving to newer
PUC releases is a baseline update, not a requirement of this release.

The following are intentionally not configurable:

- runtime values and operators' meaning;
- scope/upvalue rules;
- multiple-return rules;
- metatables;
- coroutine behavior;
- GC behavior;
- C API semantics;
- bytecode semantics;
- VM semantics.

A profile is therefore another notation for Lua, not a way to define a new
runtime language.
