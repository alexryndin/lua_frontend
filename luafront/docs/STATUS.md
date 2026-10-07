# Implementation status — 1.0.0

## Release status

The Lua 5.5 target is feature-complete for the project's design contract:
**configurable surface syntax, fixed Lua semantics**.

The primary artifact is the `lua` interpreter.  It is the PUC Lua
compiler/runtime with a configurable source frontend installed at the normal
`lua_load` boundary.

## Implemented

### Profile system

- external EBNF-like syntax profiles;
- optional profile identity/version metadata;
- ordered alternatives, sequences, optional/repetition/grouping and captures;
- fixed lexical classes `NAME`, `NUMBER`, `STRING`, `EOF`;
- grammar-discovered keywords/operators/punctuation with longest match;
- contextual literals for Lua 5.5 compatibility cases such as `global`;
- declarative AST mappings with no user executable code;
- startup validation for duplicate/undefined rules, bad captures/constructors,
  nullable repetition, left recursion, and missing `chunk`;
- compiled grammar-IR cache with exact-source verification and corruption
  recovery.

### Lua frontend

- complete reference `lua55.syntax` for the supported Lua 5.5 baseline;
- JS-like example dialect;
- Lua comments, short strings, long strings and numerals;
- binary-safe string payloads including embedded NUL;
- Lua escapes `\\xXX`, `\\ddd`, `\\u{...}`, `\\z`, escaped newline and
  CR/LF normalization;
- canonical Lua AST and source spans;
- expression precedence/associativity normalization;
- postfix call/index/field/method normalization;
- canonical validation of assignment targets and statement forms;
- typed canonical Lua-token lowering with no source-to-source translation.

### PUC integration

- alternate typed-token reader into the normal PUC parser;
- per-`lua_State` source compiler hook at `lua_load`;
- unchanged PUC semantic analysis and bytecode generator after the token
  boundary;
- profile propagation through `load`, `loadfile`, `dofile`, `require`, main
  chunks, `-e`, `LUA_INIT*`, and the REPL;
- binary chunks bypass the source frontend;
- mode and environment handling preserved for dynamic loads.

### Interpreter and tooling

- PUC-compatible `lua` command line with `--syntax` and `--native` extensions;
- profile-aware multiline REPL and expression shorthand;
- strict profile search/discovery with builtin fallback;
- `luafront` AST/token inspection utility;
- `luax` differential/development runner;
- reusable `libluafront.a` and `libluafront-lua.a`;
- public C headers and embedding examples;
- `make install` / `DESTDIR` packaging support.

## Verification status

Passing gates:

- `make test`;
- `make strict` (`-Werror` for project and runtime build);
- `make sanitize` (ASan + UBSan);
- public API external compile/run tests;
- generated Lua-vs-JS-like differential test (180 expressions);
- loader edge cases, REPL, CLI, profile discovery and cache tests;
- official Lua 5.5 corpus differential: 35/35 source files compile in both
  native and configurable paths and produce identical stripped bytecode.

See `docs/VERIFICATION-1.0.0.md` for the release record.

## Baseline

The runtime/compiler is PUC Lua **5.5.1** — the repository root this project
is integrated into (see [PUC_INTEGRATION.md](PUC_INTEGRATION.md)).

## Future enhancements (not release blockers)

These can improve the project without changing the 1.0 contract:

- additional syntax profiles;
- richer multi-error diagnostic recovery;
- larger mutation/fuzz campaigns;
- profile-cache performance telemetry;
- future PUC patch-level/version updates.
