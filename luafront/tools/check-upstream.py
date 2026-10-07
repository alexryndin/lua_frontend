#!/usr/bin/env python3
"""Verify that the repository-root lua.c is byte-for-byte PUC Lua 5.5.1 upstream.

In this in-tree layout the "vendored" CLI is the repository's own lua.c,
compiled unchanged through the compile-time alias adapter (see Makefile).
The gate guarantees it has no local modifications: the alias trick must keep
wrapping a pristine upstream file.
"""
from pathlib import Path
import hashlib
import sys

PATH = Path(__file__).resolve().parents[2] / "lua.c"
EXPECTED = "858a04c0757ab0b0f82245a194d7c78fa8b93e27"

data = PATH.read_bytes()
h = hashlib.sha1()
h.update(f"blob {len(data)}\0".encode())
h.update(data)
actual = h.hexdigest()
if actual != EXPECTED:
    print(f"{PATH}: upstream lua.c mismatch", file=sys.stderr)
    print(f"expected git blob {EXPECTED}", file=sys.stderr)
    print(f"actual   git blob {actual}", file=sys.stderr)
    raise SystemExit(1)
print(f"upstream lua.c verified: {actual}")
