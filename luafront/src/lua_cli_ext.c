#include "lua_cli_ext.h"
#include "luafront_lua.h"
#include "lauxlib.h"

static LF_Profile *cli_profile;

void lf_cli_set_profile(LF_Profile *profile) {
    cli_profile = profile;
}

lua_State *lf_cli_newstate(void) {
    lua_State *L = luaL_newstate();
    if (L != NULL && cli_profile != NULL)
        lf_lua_install_profile(L, cli_profile);
    return L;
}
