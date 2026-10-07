#!/usr/bin/env python3
"""Deterministic generated differential test for Lua vs JS-like syntax."""
from __future__ import annotations

import random
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RNG = random.Random(0x55_2026)


@dataclass(frozen=True)
class E:
    op: str
    a: object = None
    b: object = None


def num(depth: int) -> E:
    if depth <= 0 or RNG.random() < 0.30:
        return E("atom", RNG.choice(["a", "b", "c", str(RNG.randint(0, 20))]))
    choice = RNG.randrange(7)
    if choice == 0:
        return E("neg", num(depth - 1))
    if choice == 1:
        return E("bnot", num(depth - 1))
    if choice == 2:
        return E("^", num(depth - 1), E("atom", str(RNG.randint(0, 3))))
    if choice == 3:
        return E("%", num(depth - 1), E("atom", str(RNG.randint(1, 9))))
    return E(RNG.choice(["+", "-", "*"]), num(depth - 1), num(depth - 1))


def boolean(depth: int) -> E:
    if depth <= 0 or RNG.random() < 0.48:
        return E(RNG.choice(["<", "<=", ">", ">=", "==", "ne"]), num(2), num(2))
    choice = RNG.randrange(3)
    if choice == 0:
        return E("not", boolean(depth - 1))
    return E("and" if choice == 1 else "or", boolean(depth - 1), boolean(depth - 1))


PREC = {
    "or": 1, "and": 2,
    "<": 3, "<=": 3, ">": 3, ">=": 3, "==": 3, "ne": 3,
    "+": 4, "-": 4, "*": 5, "%": 5,
    "neg": 6, "bnot": 6, "not": 6,
    "^": 7, "atom": 9,
}


def render(e: E, dialect: str, parent_prec: int = 0, side: str = "") -> str:
    op = e.op
    if op == "atom":
        return str(e.a)
    p = PREC[op]
    if op in {"neg", "bnot", "not"}:
        spelling = {"neg": "- ", "bnot": "~", "not": ("not " if dialect == "lua" else "!")}[op]
        text = spelling + render(e.a, dialect, p, "right")
    else:
        spelling = op
        if op == "ne": spelling = "~=" if dialect == "lua" else "!="
        elif op == "and": spelling = "and" if dialect == "lua" else "&&"
        elif op == "or": spelling = "or" if dialect == "lua" else "||"
        left = render(e.a, dialect, p, "left")
        # '^' is right-associative; the rest are left-associative in these tests.
        right_parent = p - 1 if op == "^" else p
        right = render(e.b, dialect, right_parent, "right")
        text = f"{left} {spelling} {right}"
    need = p < parent_prec
    if p == parent_prec and side == "left" and op == "^":
        need = True
    # Random explicit parentheses also exercise Paren/multret-neutral paths.
    if parent_prec == 0 and RNG.random() < 0.08:
        need = True
    return f"({text})" if need else text


def main() -> int:
    exprs: list[E] = []
    for _ in range(180):
        exprs.append(num(3) if RNG.random() < 0.55 else boolean(3))

    lua_lines = [
        "local a, b, c = 7, 11, 3",
        "local function emit(x) print(type(x), tostring(x)) end",
    ]
    js_lines = [
        "let a = 7;", "let b = 11;", "let c = 3;",
        "function emit(x) { print(type(x), tostring(x)); }",
    ]
    for e in exprs:
        lua_lines.append(f"emit({render(e, 'lua')})")
        js_lines.append(f"emit({render(e, 'js')});")

    with tempfile.TemporaryDirectory(prefix="luafront-property-") as td:
        td = Path(td)
        lua_file = td / "generated.lua"
        js_file = td / "generated.lx"
        lua_file.write_text("\n".join(lua_lines) + "\n")
        js_file.write_text("\n".join(js_lines) + "\n")
        native = subprocess.run(
            [str(ROOT / "lua"), "--native", str(lua_file)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        custom = subprocess.run(
            [str(ROOT / "lua"), "--syntax", str(ROOT / "profiles/jslike.syntax"), str(js_file)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        if native.returncode != 0 or custom.returncode != 0:
            Path("/tmp/luafront-property-fail.lua").write_text(lua_file.read_text())
            Path("/tmp/luafront-property-fail.lx").write_text(js_file.read_text())
        if native.returncode != 0:
            raise SystemExit("native generated program failed: " + native.stderr.decode(errors="replace"))
        if custom.returncode != 0:
            raise SystemExit("custom generated program failed: " + custom.stderr.decode(errors="replace"))
        if native.stdout != custom.stdout:
            (td / "native.out").write_bytes(native.stdout)
            (td / "custom.out").write_bytes(custom.stdout)
            raise SystemExit("generated Lua/JS-like differential output mismatch")
    print(f"generated dialect differential passed: {len(exprs)} expressions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
