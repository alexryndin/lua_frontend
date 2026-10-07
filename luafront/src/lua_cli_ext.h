#ifndef LUAFRONT_LUA_CLI_EXT_H
#define LUAFRONT_LUA_CLI_EXT_H

#include <stddef.h>

typedef struct lua_State lua_State;
typedef struct LF_Profile LF_Profile;

/* Called by the unmodified PUC lua.c through a compile-time symbol alias. */
lua_State *lf_cli_newstate(void);

/* The launcher resolves the syntax configuration (entry override profile
   plus extension mappings) before PUC main runs and hands it over here;
   lf_cli_newstate consumes it when the state is created. */
typedef struct {
    char ext[20];
    char name[68];
    char path[400];
} LF_CliSyntaxMap;
void lf_cli_set_syntax_config(LF_Profile *entry_profile,
                              const LF_CliSyntaxMap *maps, size_t nmaps);

#endif
