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
- Lua `--` line/long comments;
- `NAME`;
- `NUMBER`;
- `STRING` (short and long Lua strings);
- `EOF`.

Everything else is discovered from quoted terminals in the grammar.  Literal
matching uses longest match.

`STRING` preserves Lua 5.5 lexical semantics, including binary NUL bytes,
`\\xXX`, decimal escapes, `\\u{...}`, `\\z`, escaped newlines, and long-string
newline normalization.

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
