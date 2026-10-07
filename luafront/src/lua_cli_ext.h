#ifndef LUAFRONT_LUA_CLI_EXT_H
#define LUAFRONT_LUA_CLI_EXT_H

typedef struct lua_State lua_State;
typedef struct LF_Profile LF_Profile;

/* Called by the unmodified PUC lua.c through a compile-time symbol alias. */
lua_State *lf_cli_newstate(void);

/* The launcher owns the profile and keeps it alive until PUC main returns. */
void lf_cli_set_profile(LF_Profile *profile);

#endif
