# Verification — 1.0.1

Baseline: PUC Lua 5.5.0 (upstream release); the in-tree baseline of this
repository is PUC Lua 5.5.1.

Release gates executed on 2026-10-07 after the upstream-CLI refactor.

## Upstream CLI integrity

The upstream release verified `vendor/lua55/lua.c` byte-for-byte against the
PUC Lua 5.5.0 file (Git blob `5054583de93857caa7115ed7fc520ab232ef1099`).
In this in-tree layout the equivalent gate (`tools/check-upstream.py`)
verifies the repository-root `lua.c` against the PUC Lua 5.5.1 Git blob:

```text
858a04c0757ab0b0f82245a194d7c78fa8b93e27
```

`make test`, `make strict`, and `make sanitize` all run
`tools/check-upstream.py` so an accidental CLI fork fails the gate.

The file is compiled with two symbol aliases only:

- `main -> puc_lua_main` so the small luafront launcher can pre-process
  `--syntax` / `--native` and then delegate to PUC;
- `luaL_newstate -> lf_cli_newstate` so the selected source-compiler hook is
  installed on the state created by the stock PUC CLI.

No source edit is applied to `lua.c`.

## Normal gate

```sh
make test
```

Passed, including:

- ordinary PUC CLI options and script arguments;
- `--syntax` / `--native` launcher selection;
- `LUA_INIT`, `-e`, `-l`, stdin, `-E`, project/user/env profile discovery;
- stock PUC REPL with configurable multiline syntax;
- `repl_expr` through the compiler hook, including a test profile that has no
  surface `return` keyword at all;
- loader/runtime/profile-cache tests;
- generated Lua-vs-JS-like differential (180 expressions);
- public C API compile/run tests.

## Strict gate

```sh
make strict
```

Passed with `-Werror` for project sources and vendored Lua build.

## Sanitizer gate

```sh
make sanitize
```

Passed with ASan + UBSan and leak detection enabled.

## Corpus parity

The semantic/compiler corpus result from 1.0.0 remains:

```text
native/profile compile + stripped-bytecode identical: 35 files
```

The 1.0.1 change is confined to CLI delegation and REPL/load-hook integration;
the canonical parser/lowering and PUC codegen path used by that corpus are
unchanged.
