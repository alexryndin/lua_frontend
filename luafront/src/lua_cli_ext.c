#include "lua_cli_ext.h"
#include "luafront_lua.h"
#include "lauxlib.h"

#include <stdio.h>
#include <string.h>

/* Pending CLI syntax configuration, filled by the launcher before PUC main
   creates the state, consumed once by lf_cli_newstate.  String pointers in
   cli_maps reference the launcher's storage, which outlives puc_lua_main. */
static LF_Profile *cli_entry_profile;
static LF_SyntaxMapEntry cli_maps[64];
static size_t cli_nmaps;

void lf_cli_set_syntax_config(LF_Profile *entry_profile,
                              const LF_CliSyntaxMap *maps, size_t nmaps) {
    cli_entry_profile = entry_profile;
    cli_nmaps = nmaps < 64 ? nmaps : 64;
    for (size_t i = 0; i < cli_nmaps; i++) {
        cli_maps[i].ext = maps[i].ext;
        cli_maps[i].name = maps[i].name;
        cli_maps[i].path = maps[i].path;
    }
}

lua_State *lf_cli_newstate(void) {
    lua_State *L = luaL_newstate();
    if (L == NULL) return NULL;
    LF_RegistryConfig cfg = {cli_entry_profile, cli_maps, cli_nmaps};
    char err[512];
    if (lf_lua_attach_registry(L, &cfg, err, sizeof(err)) != 0) {
        fprintf(stderr, "lua: %s\n", err);
        lua_close(L);
        return NULL;
    }
    cli_entry_profile = NULL;  /* ownership transferred to the registry */
    return L;
}
