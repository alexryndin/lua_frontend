# Design

## Contract

The frontend changes only Lua surface syntax.  Two source programs that
produce the same canonical AST have the same Lua semantics.

A syntax profile can select spelling and grammar, and can desugar syntax into
existing Lua AST constructors.  It cannot add VM operations, new runtime value
kinds, new scope rules, or new operator semantics.

## Pipeline

1. Resolve the syntax profile.
2. Load compiled grammar IR from cache or parse the profile.
3. Validate references, mappings, nullable repetitions and left recursion.
4. Discover literal terminals used by the grammar.
5. Tokenize source with fixed Lua lexical classes plus discovered literals.
6. Interpret the EBNF grammar using ordered alternatives and backtracking.
7. Evaluate declarative semantic mappings.
8. Normalize expression and postfix helpers into canonical AST nodes.
9. Validate profile-independent canonical Lua invariants.
10. Lower canonical AST to a typed canonical Lua token stream.
11. Feed those tokens directly into the normal PUC Lua parser/compiler.
12. Execute the resulting closure in the normal Lua VM.

There is no generated intermediate Lua source file or source string.

## Profile resolution

The interpreter searches, in order:

```text
explicit --syntax
LUA_SYNTAX
./.lua-syntax
$XDG_CONFIG_HOME/lua/syntax
$HOME/.config/lua/syntax
/etc/lua/syntax
embedded lua55 profile
```

Explicit and environment-selected files are strict selections: load/validation
failure is fatal.  `-E` disables environment-derived choices but not explicit,
project/system, or builtin selection.

## Compiled profile cache

`lf_profile_load` reads the external profile source, derives a content key and
looks for a serialized grammar-IR entry under the user cache directory.

A cache entry contains:

- cache format magic/version;
- the exact original profile source bytes;
- profile name/version metadata;
- contextual literals;
- grammar-expression IR;
- semantic-action IR.

On a hit, the source bytes must match exactly and the reconstructed profile is
validated again.  A corrupted entry becomes a cache miss and is replaced.
Therefore the cache cannot change language semantics; it only removes repeated
profile parsing/IR construction.

## Fixed lexical concepts

`NAME`, `NUMBER`, `STRING`, and `EOF` are fixed.  Whitespace and Lua-style
comments are handled by the frontend.  Short and long strings use Lua 5.5
lexical semantics and preserve arbitrary bytes.

Keywords, symbolic operators, punctuation and block delimiters are string
terminals in the profile.  There is no separate hard-coded keyword table in
the configurable lexer.

## Canonical AST

The canonical representation contains semantic forms needed to express Lua
5.5, including blocks and declarations, local/global functions, conditions and
loops, returns/goto/labels, calls and methods, table fields, attributes,
functions/parameters, named varargs, variables, literals, and unary/binary
expressions.

Syntax-only helper nodes such as operator tails and postfix suffixes are
eliminated during semantic mapping and do not cross the canonical boundary.

Every AST node carries a source span from the original dialect source.

## Canonical token lowering

The backend emits typed lexical tokens, not text.  A token records:

- its PUC-facing token kind or literal spelling;
- byte-length-aware payload for names, strings, and numerals;
- the original source span.

Expression emission is precedence-aware and adds parentheses where required to
preserve the AST.  Explicit source parentheses remain represented by `Paren`
because parentheses can change Lua multiple-return behavior.

The PUC lexer has a small alternate token-reader entry point.  Ordinary Lua
text and canonical tokens therefore converge before the same PUC parser and
code generator.

```text
custom source -> configurable parser -> canonical AST -> canonical tokens --+
                                                                          |
ordinary Lua source -> native lexer --------------------------------------+ 
                                                                          |
                                                                          v
                                                                     PUC parser
                                                                          |
                                                                          v
                                                                     PUC codegen
                                                                          |
                                                                          v
                                                                      bytecode
```

## Source compiler hook

The Lua API has one project-specific public extension:

```c
lua_setsourcecompiler(L, compiler, ud);
```

The hook is stored in the shared Lua state and consulted by `lua_load` for
source chunks.  `lf_lua_install_profile` installs a compiler bound to an
`LF_Profile`.

Consequently every textual compilation path reaching `lua_load` sees the same
profile: main files, command-line chunks, `load`, `loadfile`, `dofile`, Lua
modules loaded by `require`, and REPL chunks.  Binary chunks continue through
the normal undumper.

## Semantic authority

Canonical validation catches syntax-independent structural errors early, such
as an invalid assignment target.  Scope/control-flow/runtime-sensitive rules
remain owned by PUC.  Examples include:

- readonly locals/globals;
- `<close>` handling;
- goto scope checks;
- break placement;
- upvalue construction;
- multiple-return/tail-call behavior.

This avoids maintaining a second implementation of Lua semantics.

## Default Lua syntax

Standard Lua is not a separate hard-coded frontend.  `profiles/lua55.syntax`
is the reference grammar and an exact generated copy is embedded as the final
fallback.  `tools/embed-profile.py --check` prevents the checked-in embedded
copy from drifting from the reference file.

## REPL

The interactive frontend uses the same profile as all other source loads.
Completion is determined by parser state, not by looking for Lua-specific
words such as `end`.  The optional `repl_expr` rule describes expression
shorthand for a profile.

## Public embedding boundary

`libluafront.a` exposes profile/AST/token APIs.

`libluafront-lua.a` additionally bundles the PUC compiler/runtime bridge.
An embedding application can install a profile on a `lua_State` and continue
using ordinary `luaL_load*` APIs.
