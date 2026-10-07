# Syntax profile format

A profile begins with an identity:

```text
profile name;
version "1.0";       # optional metadata
```

It then contains zero or more `contextual` declarations and grammar rules:

```text
contextual "global";
rule name = grammar-expression [ => action ];
```

`version` is metadata and does not alter parsing semantics.

## Grammar operators

```text
A B C       sequence
A | B       ordered alternative
[ A ]       optional
{ A }       zero or more
( A )       grouping
"text"      literal terminal
NAME        identifier lexical class
NUMBER      numeral lexical class
STRING      literal-string lexical class
EOF         end of input
A:value     capture the value produced by A as `value`
```

Alternatives are **ordered**.  The generic parser tries them from left to
right with backtracking.  This gives ambiguity a deterministic meaning instead
of relying on generated LR conflict resolution.

Captures are lexical to their production.  A referenced subrule returns its
mapped value; its internal captures do not leak into its caller.

A `contextual` literal remains lexically a `NAME`; grammar rules can still
match that exact spelling.  The Lua 5.5 profile uses this for `global` so the
frontend matches the supported PUC compatibility behavior.

## Fixed lexical layer

These concepts are intentionally not profile-defined:

- whitespace;
- `NAME`;
- `NUMBER`;
- `STRING` (short and long Lua strings);
- `EOF`.

Everything else is discovered from quoted terminals in the grammar.  Literal
matching uses longest match.

`STRING` preserves Lua 5.5 lexical semantics, including binary NUL bytes,
`\\xXX`, decimal escapes, `\\u{...}`, `\\z`, escaped newlines, and long-string
newline normalization.

## Comments

Comments are configurable **trivia**: they are skipped by the lexer between
any two meaningful tokens and never reach the grammar or the canonical AST.
No productions are needed to allow comments between tokens — `foo(/* x */ 1,
2)` or a trailing `// comment` work automatically.

Profile directives:

```text
line_comment "--" ;              /* prefix through end of line            */
block_comment "/*" "*/" ;        /* opener and closer, non-nested,
                                    first closing delimiter wins           */
lua_long_comment "--" ;          /* marker followed by a Lua long bracket
                                    --[=*[ ... ]=*]; the bracket level is
                                    parameterized, so this is a dedicated
                                    primitive rather than a string pair    */
```

Semantics:

- A profile with **no** comment directives keeps the built-in Lua comment
  set: `--` line comments plus `--` + long bracket long comments.
- Any comment directive **replaces** that default; only the declared set
  applies.  (A dialect with no comments at all is not expressible in this
  version.)
- At each position the lexer checks whitespace, then comments, then tokens.
  Among all comment candidates (long marker + bracket, block opener, line
  prefix) the longest match wins.
- Skipping preserves line/column accounting, so spans of the following tokens
  are unaffected (including multiline block comments and Lua long comments).
- An unterminated block comment or long comment is a lexical error with a
  span pointing at the comment start.

Validation rejects, at profile load:

- delimiters that are empty or contain newlines;
- an opener that shadows a fixed lexical class (starts a `NAME`, `NUMBER`,
  `STRING` quote, or long-string `[`) — the comment scanner runs first and
  would swallow that class;
- an opener that equals or is a prefix of a grammar terminal (e.g.
  `line_comment "//"` together with a `"///"` terminal: the comment scanner
  would always win and the terminal is unreachable).  The reverse — a
  terminal that is a prefix of an opener, like `/` division with `//`
  comments — is valid: at `a / b` the comment does not match, at `a // b` the
  comment does;
- the same opener in two comment categories, e.g. `line_comment "/*"` plus
  `block_comment "/*" "*/"` (ambiguous).  Exception: `line_comment "--"` with
  `lua_long_comment "--"` is fine — the long candidate is strictly longer
  whenever a bracket actually follows.

Note: comments inside `.syntax` files themselves (`--` and `#` through end of
line) are handled by the profile DSL's own scanner and are not affected by
these directives.

## Actions

An action is a constructor expression:

```text
=> Return(values)
=> If(cond, yes, no)
=> Binary(op, left, right)
=> Pass(value)
```

Arguments can be capture references, string literals, nested constructors, or
`null`.

Profiles cannot execute C/Lua/shell callbacks.  Actions can only build the
fixed canonical Lua AST or call fixed mapping helpers.  This is the boundary
that keeps runtime semantics non-configurable.

Important helpers include:

```text
Pass(x)                 return mapped value unchanged
Tail(op, value)         internal operator tail
Fold(first, rest)       left-associative expression fold
Right(left, tail)       right-associative expression fold
Postfix(base, suffixes) call/index/field/method fold
```

Helper nodes are eliminated before the canonical AST boundary.

## Required root

Every profile must define:

```text
rule chunk = ...;
```

A profile may additionally define:

```text
rule repl_expr = ...;
```

`repl_expr` lets the interactive interpreter accept expression shorthand and
return its values without hard-coding a particular surface keyword.

## Validation

Before source code is compiled, the profile loader rejects:

- duplicate rule names;
- references to undefined rules;
- unknown canonical constructors/helpers;
- references to unknown captures;
- repetitions whose body can match empty input;
- left-recursive rule cycles;
- profiles without a `chunk` root.

Cached grammar IR is validated again after deserialization.

## Reference profiles

- `profiles/lua55.syntax` — complete reference syntax for the Lua 5.5 target;
- `profiles/jslike.syntax` — demonstration of alternate braces/operators/keywords.
