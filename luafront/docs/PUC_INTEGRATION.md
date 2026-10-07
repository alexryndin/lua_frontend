# PUC Lua 5.5.1 integration

The repository root is a PUC Lua 5.5.1 source tree carrying a deliberately
small frontend integration patch.  The PUC parser, semantic analysis, code
generator and VM remain the authoritative Lua implementation.  The frontend
itself lives in `luafront/` and builds against the repository root.

## 0. Source metadata (per-unit syntax selection)

Every text load carries `lua_SourceInfo` (origin FILE/NONFILE, role
ENTRY/MODULE/LUA_INIT/DYNAMIC, real path) from the call site that knows why
the chunk is being loaded:

```text
lua.h       lua_SourceInfo, role/origin constants
lapi.c      lua_load_source; lua_load wraps it with DYNAMIC/NONFILE
lauxlib.c   luaL_loadfilex_source / luaL_loadbufferx_source;
            plain variants wrap with DYNAMIC (origin FILE iff filename)
loadlib.c   require searcher marks MODULE
```

`lua.c` itself stays byte-for-byte upstream: the CLI compiles a generated
copy with `luafront/patches/lua-cli-sourceinfo.patch`, which marks the
entry/init call sites (dostring/-e, handle_script, REPL, handle_luainit).
`make check-upstream` still verifies the pristine `lua.c`;
`make check-lua-overlay` verifies the patch applies cleanly and carries
exactly the expected role markers.  Roles are never inferred from
chunknames.

## 1. Alternate lexical-token input

The normal parser traditionally obtains tokens only by asking `llex.c` to scan
characters.  The project adds an optional `luaX_TokenReader` to `LexState`.

Relevant symbols/files:

```text
llex.h     luaX_TokenReader, LexState tokenreader fields,
           luaX_settokeninput
llex.c     token-reader branch in luaX_next/luaX_lookahead
lparser.h  luaY_parser_tokens
lparser.c  parserbody + token-parser entry (luaY_parser_tokens)
ldo.h      luaD_protectedtokenparser
ldo.c      protected token-parser setup
```

`luafront/src/puc_bridge.c` converts canonical frontend tokens into PUC token
IDs and `SemInfo` payloads and calls this entry point.

In this tree `luaY_parser_tokens` additionally receives the scanner anchor
table (`Table *anchor`), following the anchoring scheme PUC Lua introduced
after 5.5.0; the table is created by `f_parser` as usual.

This modification is intentionally below Lua semantic parsing: all parser
logic after token acquisition is shared with native Lua source.

## 2. Per-state source compiler hook

The public Lua state has one optional source-compilation hook:

```c
lua_setsourcecompiler(L, compiler, ud);
```

Relevant files:

```text
lua.h      hook type/API declaration
lstate.h   hook + userdata in global state
lstate.c   initialization (NULL by default)
lapi.c     lua_load dispatch and setter
```

When no hook is installed, `lua_load` follows the original PUC path.  When a
profile is installed, text chunks are collected and compiled by the
configurable frontend.  Binary chunks are delegated to PUC's normal binary
parser/undumper.

This location is what makes the profile automatically apply to `load`,
`loadfile`, `dofile`, and Lua `require` without patching each library
individually.

## 3. Invariants

The integration layer does **not** modify:

- Lua opcode definitions;
- parser semantic rules;
- scope/upvalue representation;
- bytecode generation algorithms;
- VM execution;
- GC behavior;
- table/metatable behavior;
- coroutine semantics.

The official corpus bytecode differential is the regression guard for this
boundary: native source lexing and the data-driven standard profile produce
identical stripped bytecode for every available Lua 5.5 test chunk
(`make -C luafront corpus TESTES=../testes`).
