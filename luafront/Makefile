CC ?= cc
AR ?= ar
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude
LUA_CPPFLAGS = -I.. -DLUA_USE_LINUX
LUA_CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra
LDLIBS_LUA = -lm -ldl

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DATADIR ?= $(PREFIX)/share/luafront
DESTDIR ?=

SAN_CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer
SAN_LUA_CFLAGS = -std=c11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer
STRICT_CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
STRICT_LUA_CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror

FRONT_OBJ = src/frontend.o src/canonical.o src/builtin_profile.o
LUAFRONT_OBJ = $(FRONT_OBJ) src/main.o
LUAX_OBJ = $(FRONT_OBJ) src/puc_bridge.o src/luax.o
# The PUC Lua compiler/runtime is the parent directory of this project
# (the repository root).  lua.c/onelua.c provide their own mains, ltests.c
# is the internal debug build; object files are kept out of the source tree.
LUA_SRC = $(filter-out ../lua.c ../onelua.c ../ltests.c,$(wildcard ../*.c))
LUA_OBJ = $(patsubst ../%.c,obj/%.o,$(LUA_SRC))

LIBFRONT = libluafront.a
LIBFRONTLUA = libluafront-lua.a

all: lua luafront luax $(LIBFRONT) $(LIBFRONTLUA)

$(LIBFRONT): $(FRONT_OBJ)
	rm -f $@
	$(AR) rcs $@ $(FRONT_OBJ)

$(LIBFRONTLUA): $(FRONT_OBJ) src/puc_bridge.o $(LUA_OBJ)
	rm -f $@
	$(AR) rcs $@ $(FRONT_OBJ) src/puc_bridge.o $(LUA_OBJ)

luafront: $(LUAFRONT_OBJ)
	$(CC) $(CFLAGS) -o $@ $(LUAFRONT_OBJ)

luax: $(LUAX_OBJ) $(LUA_OBJ)
	$(CC) $(CFLAGS) -o $@ $(LUAX_OBJ) $(LUA_OBJ) $(LDLIBS_LUA)

lua: $(FRONT_OBJ) src/puc_bridge.o src/lua_cli.o $(LUA_OBJ)
	$(CC) $(CFLAGS) -o $@ $(FRONT_OBJ) src/puc_bridge.o src/lua_cli.o $(LUA_OBJ) $(LDLIBS_LUA)

src/frontend.o src/canonical.o src/builtin_profile.o src/main.o: src/%.o: src/%.c include/luafront.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/puc_bridge.o src/luax.o src/lua_cli.o: src/%.o: src/%.c include/luafront.h include/luafront_lua.h
	$(CC) $(CPPFLAGS) $(LUA_CPPFLAGS) $(CFLAGS) -c -o $@ $<

obj/%.o: ../%.c | obj
	$(CC) $(LUA_CPPFLAGS) $(LUA_CFLAGS) -c -o $@ $<

obj:
	mkdir -p $@

regen-profile:
	./tools/embed-profile.py

check-generated:
	./tools/embed-profile.py --check

test: all check-generated
	./tests/run.sh

# Differential compiler gate against the official Lua test corpus.
# It checks acceptance and stripped bytecode identity.
# Usage: make corpus TESTES=/path/to/lua-5.5/testes  (or TESTES=../testes)
corpus: all
	@test -n "$(TESTES)" || { echo "TESTES=/path/to/lua-5.5/testes is required" >&2; exit 2; }
	./tools/check-puc-testes.sh "$(TESTES)"

strict:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(STRICT_CFLAGS)' LUA_CFLAGS='$(STRICT_LUA_CFLAGS)' all
	./tools/embed-profile.py --check
	LUAFRONT_NO_CACHE=1 ./tests/run.sh

# Rebuild the complete frontend/backend with ASan+UBSan and run the normal gate.
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(SAN_CFLAGS)' LUA_CFLAGS='$(SAN_LUA_CFLAGS)' all
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 API_SANITIZE_FLAGS='-fsanitize=address,undefined' ./tests/run.sh

install: all
	install -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(LIBDIR)" \
	           "$(DESTDIR)$(INCLUDEDIR)" "$(DESTDIR)$(DATADIR)/profiles"
	install -m 755 lua luafront luax "$(DESTDIR)$(BINDIR)/"
	install -m 644 $(LIBFRONT) $(LIBFRONTLUA) "$(DESTDIR)$(LIBDIR)/"
	install -m 644 include/luafront.h include/luafront_lua.h \
	              ../lua.h ../luaconf.h \
	              ../lauxlib.h ../lualib.h \
	              "$(DESTDIR)$(INCLUDEDIR)/"
	install -m 644 profiles/lua55.syntax profiles/jslike.syntax \
	              "$(DESTDIR)$(DATADIR)/profiles/"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/lua" "$(DESTDIR)$(BINDIR)/luafront" "$(DESTDIR)$(BINDIR)/luax"
	rm -f "$(DESTDIR)$(LIBDIR)/$(LIBFRONT)" "$(DESTDIR)$(LIBDIR)/$(LIBFRONTLUA)"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/luafront.h" "$(DESTDIR)$(INCLUDEDIR)/luafront_lua.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/lua.h" "$(DESTDIR)$(INCLUDEDIR)/luaconf.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/lauxlib.h" "$(DESTDIR)$(INCLUDEDIR)/lualib.h"
	rm -f "$(DESTDIR)$(DATADIR)/profiles/lua55.syntax" "$(DESTDIR)$(DATADIR)/profiles/jslike.syntax"

clean:
	rm -f $(FRONT_OBJ) src/main.o src/puc_bridge.o src/luax.o src/lua_cli.o \
	      lua luafront luax $(LIBFRONT) $(LIBFRONTLUA)
	rm -rf obj

.PHONY: all test corpus strict sanitize clean install uninstall regen-profile check-generated
