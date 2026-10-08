#ifndef LUAFRONT_LUA_H
#define LUAFRONT_LUA_H

#include "luafront.h"
#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compile a canonical Lua AST with the vendored PUC Lua 5.5 compiler.
 * On success, leaves the compiled closure on top of L and returns LUA_OK.
 * On failure, leaves the normal Lua error object on the stack and mirrors
 * the textual error into err when possible.
 */
int lf_lua_loadast(lua_State *L, const LF_Ast *ast,
                   const char *chunkname, LF_Error *err);

/* Parse source through profile, lower to canonical Lua, and compile it. */
int lf_lua_loadsource(lua_State *L, LF_Profile *profile,
                      const char *chunkname, const char *source, size_t len,
                      LF_Error *err);

/* Compile one REPL input unit.  Profiles may provide `repl_expr` to define
   expression shorthand (the bundled profiles map it to a returning chunk).
   `incomplete` is set when more input could complete the current unit. */
int lf_lua_loadrepl(lua_State *L, LF_Profile *profile,
                    const char *chunkname, const char *source, size_t len,
                    int *incomplete, LF_Error *err);

/* Install profile as the compiler for every subsequent lua_load in this state
   (legacy single-profile API; the profile stays owned by the caller). */
void lf_lua_install_profile(lua_State *L, LF_Profile *profile);
void lf_lua_uninstall_profile(lua_State *L);

/* ---- per-source-unit syntax registry ---- */

typedef struct LF_ProfileRegistry LF_ProfileRegistry;

/* One extension mapping: files ending in ".ext" are compiled with the
   profile declared as `name` living in `path` (loaded lazily on first
   use; the declared profile name must match).  "lua55" and extension
   "lua" are reserved for the builtin. */
typedef struct {
    const char *ext;
    const char *name;
    const char *path;
} LF_SyntaxMapEntry;

/* Registry configuration.  `entry_profile` (may be NULL) is used for
   ENTRY-role source units only (main script, -e, REPL, stdin); ownership
   is transferred to the registry. */
typedef struct {
    LF_Profile *entry_profile;
    const LF_SyntaxMapEntry *maps;
    size_t nmaps;
} LF_RegistryConfig;

/* Attach a syntax registry: installs the source compiler, registers the
   "syntax" library in package.preload, and stores the registry as a
   Lua-owned userdata (freed on lua_close).  Returns 0 on success; on
   failure fills err and the state is left without a compiler hook. */
int lf_lua_attach_registry(lua_State *L, const LF_RegistryConfig *cfg,
                           char *err, size_t errlen);

/* C-side named lookup through the attached registry (same rules as
   require("syntax").load). */
LF_ProfileRegistry *lf_lua_get_registry(lua_State *L);
int lf_lua_loadsyntax(lua_State *L, const char *name, const char *chunkname,
                      const char *source, size_t len, const char *mode);

/* Lua-facing module: syntax.load(source, name [, chunkname [, mode [, env]]])
   registered automatically by lf_lua_attach_registry. */
int luaopen_syntax(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif
