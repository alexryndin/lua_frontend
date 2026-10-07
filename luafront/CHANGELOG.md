# Changelog

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
