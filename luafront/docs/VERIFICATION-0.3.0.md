# 0.3.0 verification

Verified on 2026-10-06 in the release workspace.

- `make test`: PASS
- `make corpus TESTES=/mnt/data/lua-configurable-frontend-work/luazig/testes`: PASS, 35/35 files compile through both native PUC and configurable `lua55.syntax` paths
- official `testes/strings.lua`: native/profile runtime stdout byte-identical
- official `testes/vararg.lua`: native/profile runtime stdout byte-identical
- `make sanitize`: PASS with ASan + UBSan
- post-sanitizer normal optimized rebuild and `make test`: PASS

The vendored PUC runtime/compiler baseline remains 5.5.0. Lua 5.5.1 vendor refresh is not claimed by this release.
