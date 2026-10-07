#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luafront-api.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc=${CC:-cc}
api_sanitize_flags=${API_SANITIZE_FLAGS:-}

# The embedding examples live as snippets in README.md; the API gate compiles
# the same code inline so the public headers stay tested with -Werror.
cat >"$tmp/parse.c" <<'EOF'
#include "luafront.h"

#include <stdio.h>

int main(void) {
    static const char source[] = "return 40 + 2";
    LF_Error err = {0};
    LF_Profile *profile = lf_profile_builtin_lua55(&err);
    if (profile == NULL) {
        fprintf(stderr, "%s\n", err.message);
        return 1;
    }
    LF_Ast *ast = lf_parse(profile, "=example", source,
                           sizeof(source) - 1, &err);
    if (ast == NULL) {
        fprintf(stderr, "%s\n", err.message);
        lf_profile_free(profile);
        return 1;
    }
    printf("profile=%s version=%s root=%s\n",
           lf_profile_name(profile),
           lf_profile_version(profile) ? lf_profile_version(profile) : "-",
           lf_ast_kind(ast));
    lf_ast_free(ast);
    lf_profile_free(profile);
    return 0;
}
EOF

cat >"$tmp/embed.c" <<'EOF'
#include "luafront_lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    static const char source[] = "let answer = 40 + 2; return answer;";
    LF_Error err = {0};
    if (argc != 2) {
        fprintf(stderr, "usage: %s PROFILE\n", argv[0]);
        return 2;
    }
    LF_Profile *profile = lf_profile_load(argv[1], &err);
    if (profile == NULL) {
        fprintf(stderr, "%s\n", err.message);
        return 1;
    }
    lua_State *L = luaL_newstate();
    if (L == NULL) return 1;
    luaL_openlibs(L);
    lf_lua_install_profile(L, profile);
    if (luaL_loadbuffer(L, source, sizeof(source) - 1, "=embedded") != LUA_OK ||
        lua_pcall(L, 0, 1, 0) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lf_lua_uninstall_profile(L);
        lf_profile_free(profile);
        lua_close(L);
        return 1;
    }
    printf("%lld\n", (long long)lua_tointeger(L, -1));
    lf_lua_uninstall_profile(L);
    lf_profile_free(profile);
    lua_close(L);
    return 0;
}
EOF

"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/include" \
  "$tmp/parse.c" "$root/libluafront.a" $api_sanitize_flags -o "$tmp/parse"
"$tmp/parse" | grep -qx 'profile=lua55 version=5.5.0 root=Chunk'

"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/include" -I"$root/.." \
  "$tmp/embed.c" "$root/libluafront-lua.a" $api_sanitize_flags -lm -ldl -o "$tmp/embed"
"$tmp/embed" "$root/profiles/jslike.syntax" | grep -qx '42'

echo 'public C API tests passed'
