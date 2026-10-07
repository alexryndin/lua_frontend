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
