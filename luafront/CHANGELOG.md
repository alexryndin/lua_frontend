# Changelog

## 1.3.0 — 2026-10-07

- the syntax profile is now a property of each source unit, not of the
  Lua state: one process mixes jslike/lua55 modules freely;
- new `lua_SourceInfo` (origin/role/real path) flows from PUC call sites
  (`lua_load_source`, `luaL_loadfilex_source`, `luaL_loadbufferx_source`);
  roles are ENTRY/MODULE/LUA_INIT/DYNAMIC and never inferred from
  chunknames;
- `lua.c` stays byte-for-byte upstream: the CLI builds a generated copy
  with `patches/lua-cli-sourceinfo.patch`, gated by `check-lua-overlay`;
- resolver order: explicit -> entry override (ENTRY units only) ->
  extension map (real FILE paths) -> builtin for .lua -> builtin default;
- extension maps via `LUA_SYNTAX_MAP` / `--syntax-map ext=name:path`
  (CLI wins, `-E` ignores the env, lazy loading, declared names, reserved
  `lua55`/`lua`), plus `LF_SYNTAX_TRACE` resolution tracing;
- new `syntax` library (`require("syntax").load(source, name, ...)`) and
  C API `lf_lua_loadsyntax`;
- `--syntax`, `LUA_SYNTAX`, `.lua-syntax` and user/system profiles now
  select the ENTRY override only: `require`d `.lua` modules return to
  Lua 5.5 (contract change; Lua-syntax modules and plain `load()` behave
  exactly like stock Lua);
- registry owns profiles for the state lifetime (Lua userdata, freed on
  `lua_close`);
- native PUC diagnostics are an instance property of the builtin lua55
  profile, not of the profile name.

## 1.2.0 — 2026-10-07

- removed the on-disk compiled-profile cache entirely: realistic profiles
  parse in under a millisecond, so the `LUAFRONT_CACHE_DIR` /
  `LUAFRONT_NO_CACHE` / `LUAFRONT_CACHE_TRACE` controls and the cache-format
  machinery are gone; external profiles are parsed on every use;
- removed the `luax` development runner and the `luafront` inspection tool;
  their scenarios are covered by `lua` itself (`--native`, `--syntax`,
  compile-only via `-e 'assert(loadfile(...))'`);
- moved the embedding examples into `README.md` snippets; the public-API
  gate compiles the same code inline with `-Werror`;
- `tests/matrix.lua` repaired to be compile-valid (it relied on the removed
  AST-dump mode).

## 1.1.0 — 2026-10-07

- moved comments out of the hardcoded lexer into the syntax profile:
  `line_comment` / `block_comment` / `lua_long_comment` directives;
- comments are pure lexer trivia — never part of the grammar, never in the
  canonical AST, allowed between any two tokens;
- profiles without comment directives keep the built-in Lua comment set;
  any directive replaces it (jslike now uses `//` and `/* ... */`);
- added lexical-conflict validation: delimiters must be non-empty and
  single-line; openers must not shadow fixed lexical classes; an opener must
  not equal or be a prefix of a grammar terminal (the reverse is allowed);
  the same opener in two comment categories is rejected;
- `lua_long_comment` is a dedicated primitive (marker + parameterized Lua
  long bracket `--[=*[ ... ]=*]`);
- jslike: `//` is now a comment, integer division is spelled `idiv`
  (canonical Lua `//` operation is unchanged);
- bumped the compiled-profile cache format (old entries are rebuilt).

## 1.0.1 — 2026-10-07

- removed the copied/adapted `src/lua_cli.c`;
- vendored the exact upstream PUC Lua 5.5.0 `lua.c` (Git blob `5054583de93857caa7115ed7fc520ab232ef1099`);
- build upstream `lua.c` unchanged, using compile-time aliases only to rename its `main` and intercept `luaL_newstate`;
- moved luafront CLI selection into a small launcher that strips `--syntax` / `--native` and then delegates all ordinary Lua option processing to PUC;
- moved profile installation into a tiny `lua_State` creation adapter;
- removed the forked/custom REPL implementation: PUC's stock REPL now works through the source-compiler hook, including profile `repl_expr` and `<eof>` incomplete-input handling;
- added an upstream-integrity gate for the vendored `lua.c`;
- preserved the complete CLI/search/cache/API/property test suite under normal, `-Werror`, ASan and UBSan builds.

## 1.0.0 — 2026-10-06

- completed a PUC-compatible `lua` CLI with `--syntax` and `--native`;
- made `-e`, `LUA_INIT*`, stdin, script args, `-l`, `-E`, `-W`, and the REPL profile-aware;
- added grammar-driven REPL expression shorthand and fixed incomplete-input EOF diagnostics;
- centralized strict profile discovery with project/user/system/builtin fallback;
- added optional profile version metadata;
- implemented a versioned compiled grammar-IR cache with exact-source verification, atomic replacement, corruption recovery, and disable/trace controls;
- added `libluafront.a` and `libluafront-lua.a`, public embedding examples, and `make install`/`DESTDIR`;
- made the embedded Lua profile generated from and checked against `profiles/lua55.syntax`;
- removed the strict-C overlength-string warning from the embedded profile representation;
- expanded dynamic loader tests for custom readers, environments, text/binary modes, and binary `loadfile`;
- added full CLI/search/cache/API test gates and a deterministic 180-expression Lua-vs-JS-like differential;
- strengthened the official Lua 5.5 corpus gate: all 35/35 files now produce byte-for-byte identical stripped bytecode through native PUC and the configurable `lua55.syntax` path;
- passed normal, `-Werror`, ASan, and UBSan release gates.

## 0.3.0 — 2026-10-06

- made string/token/AST payloads binary-safe by carrying explicit byte lengths all
  the way into PUC `TString`; embedded `\0` no longer truncates values;
- completed Lua 5.5 short-string escape handling (`\xXX`, `\ddd`, `\u{...}`,
  `\z`, escaped newlines) and long-string newline normalization;
- added VM regressions for binary strings and malformed escape rejection;
- added a native-parser execution/check mode to `luax` for differential tests;
- added runtime differential coverage for multiple returns, parenthesized calls,
  named varargs, short-circuit operators, tail calls, loops, `<const>`, and goto;
- added `tools/check-puc-testes.sh` and a `make corpus TESTES=...` gate;
- verified all 35 files in the available official Lua 5.5 `testes` corpus compile
  through both the native PUC parser and the configurable `lua55.syntax` path;
- verified `strings.lua` and `vararg.lua` execute with byte-identical stdout in
  native and configurable modes;
- added a reproducible `make sanitize` ASan+UBSan gate and passed it.

## 0.2.0 — 2026-10-06

- added canonical-AST to typed-Lua-token lowering;
- added a token-reader input path to the vendored PUC Lua parser;
- added end-to-end compilation and execution in the PUC Lua VM;
- added the per-state `lua_load` source-compiler hook;
- propagated the selected syntax profile through `load`, `loadfile`, `dofile`, and
  Lua-module `require`;
- kept binary chunks on the native PUC undumper path;
- expanded the standard profile for Lua 5.5 declarations, attributes, function-name
  forms, table-field forms, `elseif`, and named varargs;
- fixed repeated-capture flattening and zero-argument method calls;
- added precedence-aware canonical token emission while preserving explicit parentheses
  and Lua multiple-return semantics;
- added the `luax` executable runner and embedded standard-profile fallback;
- added runtime semantic, dynamic-loader, malformed-profile, and malformed-program tests;
- validated all 35 files in the available PUC Lua 5.5 test corpus through the
  configurable frontend and PUC compiler;
- ran the project gate under ASan and UBSan.

## 0.1.0

- initial runtime EBNF-like profile parser;
- configurable lexer and parser;
- declarative semantic mappings;
- canonical Lua AST and validation;
- standard Lua 5.5 and JS-like syntax profiles;
- AST/debug CLI.
