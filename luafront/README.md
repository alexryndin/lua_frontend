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

Release status: **1.0.0 / feature-complete for the stated design**.

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
other surface forms into canonical Lua constructs.  No JavaScript runtime
semantics are introduced.

## `lua` command line

The primary executable is a PUC-compatible Lua interpreter with two additional
options:

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

The selected profile applies consistently to:

- the main script;
- `-e` chunks;
- `LUA_INIT_5_5` / `LUA_INIT`;
- interactive REPL input;
- `load`;
- `loadfile`;
- `dofile`;
- Lua modules loaded by `require`.

Binary chunks always bypass the configurable source frontend and go directly
to PUC's binary undumper.  `load(..., "b")` / `load(..., "t")` mode checks and
custom `load` readers preserve PUC behavior.

The REPL is profile-aware.  Profiles can define the optional `repl_expr` rule
to provide expression shorthand without hard-coding Lua keywords in the CLI.

## Profile discovery

Without `--syntax` or `--native`, the interpreter resolves a profile in this
order:

1. `LUA_SYNTAX`;
2. `./.lua-syntax`;
3. `$XDG_CONFIG_HOME/lua/syntax`;
4. `$HOME/.config/lua/syntax`;
5. `/etc/lua/syntax`;
6. the embedded Lua 5.5 profile.

An explicitly selected profile and `LUA_SYNTAX` are strict: if the selected
file cannot be loaded or validated, startup fails instead of silently changing
the language.

`-E` ignores environment-driven selections (`LUA_SYNTAX`, XDG/HOME config and
`LUA_INIT*`) just as it disables other Lua environment configuration.  An
explicit `--syntax`, project `.lua-syntax`, system profile, or builtin profile
can still be used.

## Compiled profile cache

External syntax files are compiled to grammar IR and cached automatically.
The default cache directory is:

```text
$XDG_CACHE_HOME/luafront/profiles
```

or, when `XDG_CACHE_HOME` is unset:

```text
$HOME/.cache/luafront/profiles
```

Controls:

```text
LUAFRONT_CACHE_DIR=/path    override the cache directory
LUAFRONT_NO_CACHE=1         disable profile caching
LUAFRONT_CACHE_TRACE=1      print cache hit/miss/store diagnostics
```

The cache is only an optimization.  Every cache entry contains the exact
profile source bytes and a versioned grammar IR.  Source bytes are compared
before an entry is accepted, and the reconstructed IR is validated again.
Corrupt/stale entries are ignored and rebuilt.  A cache hash collision cannot
select a grammar for different source text.

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

`NAME`, `NUMBER`, `STRING`, and `EOF` are fixed lexical classes.  Whitespace,
Lua short/long strings and Lua-style comments are fixed lexical concepts.
Keywords, operators, punctuation and delimiters come from string terminals in
the profile and use longest-match tokenization.

The profile language contains no executable callbacks.  Semantic mappings may
only construct the fixed canonical Lua AST or use fixed mapping helpers such as
`Pass`, `Fold`, `Right`, and `Postfix`.

See [docs/PROFILE_FORMAT.md](docs/PROFILE_FORMAT.md).

## Architecture

```text
syntax profile
     |
     v
profile parser -> grammar IR -> validator -> compiled-profile cache
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

`lua` is the finished interpreter.

`luafront` is an AST/compiler-boundary inspection tool:

```sh
./luafront --check-profile profiles/lua55.syntax
./luafront --syntax profiles/jslike.syntax tests/jslike.lua
./luafront --tokens --syntax profiles/lua55.syntax tests/standard.lua
```

`luax` is a small non-interactive development runner retained for differential
and compiler tests:

```sh
./luax --native file.lua
./luax --syntax profiles/lua55.syntax file.lua
./luax --check --syntax profiles/lua55.syntax file.lua
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

Small embedding examples are in `examples/parse.c` and `examples/embed.c`.

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

Official Lua 5.5 syntax/compiler differential gate (the repository's own
corpus works directly):

```sh
make -C luafront corpus TESTES=../testes
```

On the official Lua 5.5 corpus, all **35/35** `testes/*.lua` files:

- compile through the native PUC parser;
- compile through `lua55.syntax` and the configurable frontend;
- produce **byte-for-byte identical stripped bytecode** in both paths.

The regular test gate additionally covers the full CLI, REPL, profile search,
profile cache corruption/recovery, binary-safe strings, all Lua short-string
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
