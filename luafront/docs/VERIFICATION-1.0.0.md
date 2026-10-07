# Verification — 1.0.0

Baseline: PUC Lua 5.5.0.

Release gates executed on 2026-10-06:

## Normal gate

```sh
make test
```

Passed:

- profile validation failures and success cases;
- Lua 5.5 and JS-like AST parsing;
- canonical validation;
- PUC compile/execute path;
- `load` / `loadfile` / `dofile` / `require`;
- text/binary load modes and custom readers;
- binary-safe strings and Lua escape rules;
- compiler-owned semantic errors;
- native/profile runtime differential;
- full `lua` CLI and profile-aware REPL;
- profile discovery precedence and `-E`;
- compiled profile cache miss/hit/corruption recovery/disable;
- deterministic generated Lua-vs-JS-like differential (180 expressions);
- external compilation/execution against both public static libraries.

## Strict warnings gate

```sh
make strict
```

Passed with `-Werror` for project sources and the vendored Lua runtime build.

## Sanitizer gate

```sh
make sanitize
```

Passed with AddressSanitizer + UndefinedBehaviorSanitizer and leak detection
enabled for the regular project test gate.

## Official Lua 5.5 corpus differential

```sh
make corpus TESTES=/mnt/data/lua-configurable-frontend-work/luazig/testes
```

Result:

```text
native/profile compile + stripped-bytecode identical: 35 files
```

All 35 available official `testes/*.lua` chunks compile through both the
native PUC lexer/parser path and the data-driven `lua55.syntax` path.  After
debug information is stripped, `string.dump` output is byte-for-byte
identical for every file.

Stripping is intentional for this semantic/codegen gate: source/debug line
metadata is tested independently, and alternate surface syntaxes naturally
have different textual positions.

## Packaging/API gate

`make install DESTDIR=...` was exercised.  External C programs were compiled
against the installed `libluafront.a` and `libluafront-lua.a` and executed
successfully.
