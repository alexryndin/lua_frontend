#include "luafront_lua.h"

/* PUC Lua internal compiler interface. */
#include "ldo.h"
#include "llex.h"
#include "lobject.h"
#include "lzio.h"
#include "lauxlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const LF_CanonTokens *tokens;
    size_t index;
} TokenSource;

typedef struct { const char *s; int token; } LiteralToken;

static const LiteralToken literals[] = {
    {"and", TK_AND}, {"break", TK_BREAK}, {"do", TK_DO},
    {"else", TK_ELSE}, {"elseif", TK_ELSEIF}, {"end", TK_END},
    {"false", TK_FALSE}, {"for", TK_FOR}, {"function", TK_FUNCTION},
    {"global", TK_GLOBAL}, {"goto", TK_GOTO}, {"if", TK_IF},
    {"in", TK_IN}, {"local", TK_LOCAL}, {"nil", TK_NIL},
    {"not", TK_NOT}, {"or", TK_OR}, {"repeat", TK_REPEAT},
    {"return", TK_RETURN}, {"then", TK_THEN}, {"true", TK_TRUE},
    {"until", TK_UNTIL}, {"while", TK_WHILE},
    {"//", TK_IDIV}, {"..", TK_CONCAT}, {"...", TK_DOTS},
    {"==", TK_EQ}, {">=", TK_GE}, {"<=", TK_LE}, {"~=", TK_NE},
    {"<<", TK_SHL}, {">>", TK_SHR}, {"::", TK_DBCOLON},
};

static int literal_token(LexState *ls, const char *s) {
    for (size_t i = 0; i < sizeof(literals) / sizeof(literals[0]); i++)
        if (strcmp(s, literals[i].s) == 0)
            return literals[i].token;
    if (s[0] != '\0' && s[1] == '\0')
        return (unsigned char)s[0];
    luaX_syntaxerror(ls, "invalid canonical Lua token");
}

static int canonical_reader(LexState *ls, void *ud, SemInfo *si) {
    TokenSource *src = (TokenSource *)ud;
    if (src->index >= lf_canonical_token_count(src->tokens))
        return TK_EOS;

    const LF_CanonToken *t = lf_canonical_token_at(src->tokens, src->index++);
    if (!t) return TK_EOS;
    if (t->span.line_start)
        ls->linenumber = (int)t->span.line_start;

    switch (t->kind) {
    case LF_CT_EOF:
        return TK_EOS;
    case LF_CT_LITERAL:
        return literal_token(ls, t->text);
    case LF_CT_NAME:
        si->ts = luaX_newstring(ls, t->text, t->text_len);
        return TK_NAME;
    case LF_CT_STRING:
        si->ts = luaX_newstring(ls, t->text, t->text_len);
        return TK_STRING;
    case LF_CT_NUMBER: {
        TValue v;
        if (luaO_str2num(t->text, &v) == 0)
            luaX_syntaxerror(ls, "malformed number in canonical AST");
        if (ttisinteger(&v)) {
            si->i = ivalue(&v);
            return TK_INT;
        }
        if (ttisfloat(&v)) {
            si->r = fltvalue(&v);
            return TK_FLT;
        }
        luaX_syntaxerror(ls, "invalid numeric value in canonical AST");
    }
    }
    luaX_syntaxerror(ls, "invalid canonical token kind");
}

static void mirror_lua_error(lua_State *L, LF_Error *err) {
    if (!err) return;
    err->kind = LF_ERROR_COMPILER;
    const char *msg = lua_tostring(L, -1);
    snprintf(err->message, sizeof(err->message), "%s",
             msg ? msg : "Lua compiler error");
}

/* Compile an already-canonical AST, leaving environment binding to caller. */
static int compile_ast_raw(lua_State *L, const LF_Ast *ast,
                           const char *chunkname, LF_Error *err) {
    LF_CanonTokens *tokens = lf_ast_to_canonical_tokens(ast, err);
    if (!tokens) {
        lua_pushstring(L, (err && err->message[0]) ? err->message
                                                   : "canonical lowering failed");
        return LUA_ERRSYNTAX;
    }
    TokenSource src = { tokens, 0 };
    int status = (int)luaD_protectedtokenparser(
        L, chunkname ? chunkname : "=(luafront)", canonical_reader, &src);
    if (status != LUA_OK) mirror_lua_error(L, err);
    lf_canonical_tokens_free(tokens);
    return status;
}

int lf_lua_loadast(lua_State *L, const LF_Ast *ast,
                   const char *chunkname, LF_Error *err) {
    if (err) memset(err, 0, sizeof(*err));
    int status = compile_ast_raw(L, ast, chunkname, err);
    if (status == LUA_OK) {
        /* Match lua_load when this API is used directly. */
        lua_pushglobaltable(L);
        (void)lua_setupvalue(L, -2, 1);
    }
    return status;
}

int lf_lua_loadsource(lua_State *L, LF_Profile *profile,
                      const char *chunkname, const char *source, size_t len,
                      LF_Error *err) {
    LF_Ast *ast = lf_parse(profile,
                           chunkname ? chunkname : "=(luafront)",
                           source, len, err);
    if (!ast) {
        lua_pushstring(L, (err && err->message[0]) ? err->message
                                                   : "frontend parse failed");
        return LUA_ERRSYNTAX;
    }
    int status = lf_lua_loadast(L, ast, chunkname, err);
    lf_ast_free(ast);
    return status;
}


int lf_lua_loadrepl(lua_State *L, LF_Profile *profile,
                    const char *chunkname, const char *source, size_t len,
                    int *incomplete, LF_Error *err) {
    LF_Error exprerr={0}, chunkerr={0};
    LF_Ast *ast=NULL;
    if (incomplete) *incomplete=0;
    if (err) memset(err,0,sizeof(*err));

    if (lf_profile_has_rule(profile,"repl_expr"))
        ast=lf_parse_rule(profile,"repl_expr",chunkname?chunkname:"=stdin",
                          source,len,&exprerr);
    if (!ast)
        ast=lf_parse(profile,chunkname?chunkname:"=stdin",source,len,&chunkerr);

    if (!ast) {
        int inc=(exprerr.kind==LF_ERROR_INCOMPLETE ||
                 chunkerr.kind==LF_ERROR_INCOMPLETE);
        if (incomplete) *incomplete=inc;
        LF_Error *best = (chunkerr.kind==LF_ERROR_INCOMPLETE) ? &chunkerr :
                         (exprerr.kind==LF_ERROR_INCOMPLETE) ? &exprerr :
                         (chunkerr.message[0] ? &chunkerr : &exprerr);
        if (err) *err=*best;
        /* Match lua_load's failure contract even for incomplete REPL input:
           a syntax error object is always left on the stack.  The CLI can
           discard it when it decides to read a continuation line. */
        lua_pushstring(L,best->message[0]?best->message:"frontend parse failed");
        return LUA_ERRSYNTAX;
    }

    int status=lf_lua_loadast(L,ast,chunkname?chunkname:"=stdin",err);
    lf_ast_free(ast);
    return status;
}

typedef struct {
    LF_Profile *profile;        /* resolved profile */
    LF_ProfileRegistry *registry;
    const lua_SourceInfo *info;
    lua_Reader reader;
    void *reader_data;
    const char *chunkname;
    const char *mode;
    char *buf;
    size_t len;
    size_t cap;
} HookJob;

static void hook_append(HookJob *j, const char *p, size_t n) {
    if (n == 0) return;
    if (j->len + n + 1 > j->cap) {
        size_t nc = j->cap ? j->cap : 4096;
        while (nc < j->len + n + 1) nc *= 2;
        char *nb = (char *)realloc(j->buf, nc);
        if (!nb) abort();
        j->buf = nb; j->cap = nc;
    }
    memcpy(j->buf + j->len, p, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

typedef struct { const char *p; size_t n; int sent; } OneReader;
static const char *one_reader(lua_State *L, void *ud, size_t *sz) {
    (void)L;
    OneReader *r=(OneReader *)ud;
    if (r->sent) { *sz=0; return NULL; }
    r->sent=1; *sz=r->n; return r->p;
}

/* ---- profile registry and per-source-unit resolution ---- */

#define LF_REGISTRY_MAGIC 0x4c465245  /* "LFRE" */

static char lf_registry_key;  /* static address: registry-table key */

static int source_compiler_hook(lua_State *L, lua_Reader reader, void *data,
                                const char *chunkname, const char *mode,
                                const lua_SourceInfo *info, void *ud);

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p) abort();
    memcpy(p, s, n + 1);
    return p;
}

typedef struct {
    char *ext;
    char *name;    /* declared profile name */
    char *path;
    LF_Profile *profile;  /* lazily loaded */
} RegistryEntry;

struct LF_ProfileRegistry {
    int magic;
    LF_Profile *override_all;   /* legacy single-profile install; NOT owned */
    LF_Profile *builtin;        /* owned builtin lua55 */
    LF_Profile *entry_override; /* owned; may be NULL */
    RegistryEntry *entries;
    size_t nentries, capentries;
};

static void trace_resolution(const char *chunkname, const lua_SourceInfo *info,
                             const char *pname, const char *reason) {
    const char *origin = (info && info->origin == LUA_SOURCE_FILE) ? "file" : "nonfile";
    const char *role = "dynamic";
    switch (info ? info->role : LUA_ROLE_DYNAMIC) {
        case LUA_ROLE_ENTRY: role = "entry"; break;
        case LUA_ROLE_MODULE: role = "module"; break;
        case LUA_ROLE_LUAINIT: role = "luainit"; break;
        default: break;
    }
    fprintf(stderr, "[syntax] %s\n         origin=%s role=%s\n"
                    "         profile=%s reason=%s\n",
            chunkname, origin, role, pname, reason);
}

static int trace_enabled(void) {
    const char *x = getenv("LF_SYNTAX_TRACE");
    return x && *x && strcmp(x, "0") != 0;
}

static void err_set(char *err, size_t errlen, const char *msg) {
    if (errlen == 0) return;
    size_t n = strlen(msg);
    if (n >= errlen) n = errlen - 1;
    memcpy(err, msg, n);
    err[n] = '\0';
}

/* Load a lazy map entry (first use).  Returns NULL and fills 'err' on
** failure; the declared profile name must match the map's name. */
static LF_Profile *entry_load(RegistryEntry *e, char *err, size_t errlen) {
    if (e->profile) return e->profile;
    char msg[768];
    LF_Error fe = {0};
    LF_Profile *p = lf_profile_load(e->path, &fe);
    if (!p) {
        snprintf(msg, sizeof(msg), "cannot load syntax profile '%s' for "
                                   "extension '.%s' from %s: %s",
                 e->name, e->ext, e->path,
                 fe.message[0] ? fe.message : "profile load failed");
        err_set(err, errlen, msg);
        return NULL;
    }
    const char *declared = lf_profile_name(p);
    if (!declared || strcmp(declared, e->name) != 0) {
        snprintf(msg, sizeof(msg), "syntax profile %s declares itself '%s' "
                                   "but is mapped as '%s'", e->path,
                 declared ? declared : "?", e->name);
        err_set(err, errlen, msg);
        lf_profile_free(p);
        return NULL;
    }
    e->profile = p;
    return p;
}

/* The resolver: explicit-all (legacy install) -> entry override -> extension
** map (real FILE paths only) -> builtin for .lua -> builtin default.
** Neither chunkname nor profile-file basenames participate. */
static LF_Profile *resolve_profile(LF_ProfileRegistry *r,
                                   const lua_SourceInfo *info,
                                   const char **reason, char *err,
                                   size_t errlen) {
    if (r->override_all) {
        *reason = "explicit-all";
        return r->override_all;
    }
    if (info && info->role == LUA_ROLE_ENTRY && r->entry_override) {
        *reason = "entry-override";
        return r->entry_override;
    }
    if (info && info->origin == LUA_SOURCE_FILE && info->path) {
        const char *ext = strrchr(info->path, '.');
        if (ext && ext[1]) {
            ext++;
            for (size_t i = 0; i < r->nentries; i++) {
                if (strcmp(r->entries[i].ext, ext) == 0) {
                    LF_Profile *p = entry_load(&r->entries[i], err, errlen);
                    if (!p) return NULL;
                    *reason = "extension-map";
                    return p;
                }
            }
            if (strcmp(ext, "lua") == 0) {
                *reason = "builtin-.lua";
                return r->builtin;
            }
        }
    }
    *reason = "state-default";
    return r->builtin;
}

/* Registry lookup by declared name for syntax.load / lf_lua_loadsyntax:
** "lua55" resolves to the builtin instance, other names to map entries. */
static LF_Profile *registry_find(LF_ProfileRegistry *r, const char *name,
                                 char *err, size_t errlen) {
    if (strcmp(name, "lua55") == 0) return r->builtin;
    for (size_t i = 0; i < r->nentries; i++) {
        if (strcmp(r->entries[i].name, name) == 0)
            return entry_load(&r->entries[i], err, errlen);
    }
    if (r->override_all && strcmp(lf_profile_name(r->override_all), name) == 0)
        return r->override_all;
    if (r->entry_override && strcmp(lf_profile_name(r->entry_override), name) == 0)
        return r->entry_override;
    snprintf(err, errlen, "no syntax profile '%s' in registry", name);
    return NULL;
}

static int registry_gc(lua_State *L) {
    LF_ProfileRegistry *r = (LF_ProfileRegistry *)lua_touserdata(L, 1);
    if (!r || r->magic != LF_REGISTRY_MAGIC) return 0;
    if (r->builtin) lf_profile_free(r->builtin);
    if (r->entry_override) lf_profile_free(r->entry_override);
    for (size_t i = 0; i < r->nentries; i++) {
        free(r->entries[i].ext);
        free(r->entries[i].name);
        free(r->entries[i].path);
        if (r->entries[i].profile) lf_profile_free(r->entries[i].profile);
    }
    free(r->entries);
    /* override_all is not owned by the registry (legacy API contract) */
    return 0;
}

static LF_ProfileRegistry *registry_new(lua_State *L) {
    LF_ProfileRegistry *r = (LF_ProfileRegistry *)lua_newuserdatauv(L,
                                        sizeof(LF_ProfileRegistry), 0);
    memset(r, 0, sizeof(*r));
    r->magic = LF_REGISTRY_MAGIC;
    if (luaL_newmetatable(L, "luafront.registry")) {
        lua_pushcfunction(L, registry_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return r;  /* on stack top */
}

static void registry_store_ref(lua_State *L) {
    /* registry userdata is on stack top; keep it findable for C lookups */
    lua_pushlightuserdata(L, (void *)&lf_registry_key);
    lua_pushvalue(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);
}

static int registry_add_entry(LF_ProfileRegistry *r, const char *ext,
                              const char *name, const char *path, char *err,
                              size_t errlen) {
    if (strcmp(name, "lua55") == 0) {
        snprintf(err, errlen, "syntax profile name 'lua55' is reserved "
                              "for the builtin profile");
        return 0;
    }
    if (strcmp(ext, "lua") == 0) {
        snprintf(err, errlen, "extension 'lua' is reserved for builtin lua55");
        return 0;
    }
    for (size_t i = 0; i < r->nentries; i++) {
        if (strcmp(r->entries[i].ext, ext) == 0) {
            snprintf(err, errlen, "duplicate syntax mapping for extension '.%s'",
                     ext);
            return 0;
        }
        if (strcmp(r->entries[i].name, name) == 0 &&
            strcmp(r->entries[i].path, path) != 0) {
            snprintf(err, errlen, "syntax profile name '%s' mapped to two "
                                  "different files: %s and %s",
                     name, r->entries[i].path, path);
            return 0;
        }
    }
    if (r->nentries == r->capentries) {
        r->capentries = r->capentries ? r->capentries * 2 : 8;
        r->entries = (RegistryEntry *)realloc(r->entries,
                          r->capentries * sizeof(*r->entries));
        if (!r->entries) abort();
    }
    RegistryEntry *e = &r->entries[r->nentries++];
    e->ext = dup_str(ext);
    e->name = dup_str(name);
    e->path = dup_str(path);
    e->profile = NULL;
    return 1;
}

static void install_syntax_preload(lua_State *L) {
    /* registry userdata is on stack top */
    luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
    lua_pushvalue(L, -2);  /* copy registry userdata as upvalue */
    lua_pushcclosure(L, luaopen_syntax, 1);
    lua_setfield(L, -2, "syntax");
    lua_pop(L, 1);  /* _PRELOAD */
}

int lf_lua_attach_registry(lua_State *L, const LF_RegistryConfig *cfg,
                           char *err, size_t errlen) {
    LF_Error fe = {0};
    LF_ProfileRegistry *r = registry_new(L);  /* userdata on top */
    r->builtin = lf_profile_builtin_lua55(&fe);
    if (!r->builtin) {
        snprintf(err, errlen, "cannot build builtin lua55 profile: %s",
                 fe.message[0] ? fe.message : "internal error");
        return 1;
    }
    if (cfg) {
        r->entry_override = cfg->entry_profile;  /* ownership transferred */
        for (size_t i = 0; i < cfg->nmaps; i++) {
            if (!registry_add_entry(r, cfg->maps[i].ext, cfg->maps[i].name,
                                    cfg->maps[i].path, err, errlen))
                return 1;
        }
    }
    install_syntax_preload(L);
    registry_store_ref(L);
    lua_setsourcecompiler(L, source_compiler_hook, r);
    lua_pop(L, 1);  /* registry userdata */
    return 0;
}

LF_ProfileRegistry *lf_lua_get_registry(lua_State *L) {
    lua_pushlightuserdata(L, (void *)&lf_registry_key);
    lua_gettable(L, LUA_REGISTRYINDEX);
    if (!lua_isuserdata(L, -1)) {
        lua_pop(L, 1);
        return NULL;
    }
    LF_ProfileRegistry *r = (LF_ProfileRegistry *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!r || r->magic != LF_REGISTRY_MAGIC) return NULL;
    return r;
}

int lf_lua_loadsyntax(lua_State *L, const char *name, const char *chunkname,
                      const char *source, size_t len, const char *mode) {
    char err[512];
    LF_ProfileRegistry *r = lf_lua_get_registry(L);
    if (!r) {
        lua_pushstring(L, "no syntax registry attached to this state");
        return LUA_ERRSYNTAX;
    }
    LF_Profile *p = registry_find(r, name, err, sizeof(err));
    if (!p) {
        lua_pushstring(L, err);
        return LUA_ERRSYNTAX;
    }
    if (mode && strchr(mode, 't') == NULL) {
        lua_pushfstring(L, "attempt to load a text chunk (mode is '%s')", mode);
        return LUA_ERRSYNTAX;
    }
    return lf_lua_loadsource(L, p, chunkname, source, len, NULL);
}

/* ---- the "syntax" library ---- */

static int syntax_load(lua_State *L) {
    /* upvalue 1: registry userdata */
    size_t len;
    const char *src = luaL_checklstring(L, 1, &len);
    const char *name = luaL_checkstring(L, 2);
    const char *chunkname = luaL_optstring(L, 3, "=(syntax)");
    const char *mode = luaL_optstring(L, 4, "bt");
    int hasenv = !lua_isnoneornil(L, 5);
    char err[512];
    LF_ProfileRegistry *r = (LF_ProfileRegistry *)lua_touserdata(L,
                                        lua_upvalueindex(1));
    if (!r || r->magic != LF_REGISTRY_MAGIC)
        return luaL_error(L, "syntax library used without a registry");
    LF_Profile *p = registry_find(r, name, err, sizeof(err));
    if (!p) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    if (strchr(mode, 't') == NULL) {
        lua_pushnil(L);
        lua_pushfstring(L, "attempt to load a text chunk (mode is '%s')", mode);
        return 2;
    }
    LF_Error fe = {0};
    LF_Ast *ast = lf_parse(p, chunkname, src, len, &fe);
    if (!ast) {
        lua_pushnil(L);
        if (fe.span.line_start)
            lua_pushfstring(L, "%s:%d:%d: %s", chunkname,
                            (int)fe.span.line_start, (int)fe.span.col_start,
                            fe.message[0] ? fe.message : "parse failed");
        else
            lua_pushstring(L, fe.message[0] ? fe.message : "parse failed");
        return 2;
    }
    int status = compile_ast_raw(L, ast, chunkname, &fe);
    lf_ast_free(ast);
    if (status != LUA_OK) {
        /* error object is on the stack from the failed compile */
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (hasenv) {
        lua_pushvalue(L, 5);
        lua_setupvalue(L, -2, 1);
    }
    return 1;
}

int luaopen_syntax(lua_State *L) {
    lua_newtable(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushcclosure(L, syntax_load, 1);
    lua_setfield(L, -2, "load");
    return 1;
}

/* ---- source compiler hook ---- */

static void compile_hook_job(lua_State *L, void *ud) {
    HookJob *j=(HookJob *)ud;
    for (;;) {
        size_t n=0;
        const char *p=j->reader(L,j->reader_data,&n);
        /* PUC ZIO treats either NULL or a zero-length chunk as EOF.  Lua's
           generic `load` reader can legitimately return an empty string;
           continuing here would call the reader forever. */
        if (!p || n==0) break;
        hook_append(j,p,n);
    }
    if (!j->buf) { j->buf=(char *)malloc(1); if(!j->buf) abort(); j->buf[0]='\0'; }

    /* Binary chunks bypass profile RESOLUTION itself (not merely profile
       parsing): a broken lazy map can never affect a binary load. */
    if (j->len > 0 && (unsigned char)j->buf[0] == (unsigned char)LUA_SIGNATURE[0]) {
        OneReader mr={j->buf,j->len,0};
        ZIO z;
        luaZ_init(L,&z,one_reader,&mr);
        int st=(int)luaD_protectedparser(L,&z,j->chunkname,j->mode);
        if (st != LUA_OK) luaD_throw(L,cast(TStatus,st));
        return;
    }

    if (j->mode && strchr(j->mode,'t') == NULL) {
        lua_pushfstring(L,"attempt to load a text chunk (mode is '%s')",j->mode);
        luaD_throw(L,LUA_ERRSYNTAX);
    }

    /* resolve the syntax profile for THIS source unit */
    char rerr[512] = {0};
    const char *reason = "state-default";
    j->profile = resolve_profile(j->registry, j->info, &reason,
                                 rerr, sizeof(rerr));
    if (!j->profile) {
        lua_pushfstring(L, "%s: %s", j->chunkname, rerr);
        luaD_throw(L, LUA_ERRSYNTAX);
    }
    if (trace_enabled())
        trace_resolution(j->chunkname, j->info, lf_profile_name(j->profile),
                         reason);

    LF_Error e={0};

    /* Stock PUC's REPL probes each line as `return <line>;` before trying it
       as a statement.  Keep lua.c completely upstream by recognizing only
       that probe at the compiler-hook boundary and delegating the original
       line to the profile's optional `repl_expr` rule. */
    if (strcmp(j->chunkname, "=stdin") == 0 &&
        lf_profile_has_rule(j->profile, "repl_expr") && j->len >= 8 &&
        memcmp(j->buf, "return ", 7) == 0 && j->buf[j->len - 1] == ';') {
        LF_Ast *rast = lf_parse_rule(j->profile, "repl_expr", "=stdin",
                                     j->buf + 7, j->len - 8, &e);
        if (rast != NULL) {
            int rst = compile_ast_raw(L, rast, j->chunkname, &e);
            lf_ast_free(rast);
            if (rst != LUA_OK) luaD_throw(L, cast(TStatus, rst));
            return;
        }
        if (e.kind == LF_ERROR_INCOMPLETE)
            lua_pushfstring(L, "%s <eof>", e.message[0] ? e.message :
                                              "incomplete expression");
        else
            lua_pushstring(L, e.message[0] ? e.message :
                                             "not a REPL expression");
        luaD_throw(L, LUA_ERRSYNTAX);
    }
    if (getenv("LF_TRACE") && j->len > 1000) fprintf(stderr,"[lf] parse len=%zu\n",j->len);
    LF_Ast *ast=lf_parse(j->profile,j->chunkname,j->buf,j->len,&e);
    if (getenv("LF_TRACE") && j->len > 1000) fprintf(stderr,"[lf] parse done ast=%p\n",(void*)ast);
    if (!ast) {
        /* For the trusted builtin Lua profile, keep PUC's user-visible
           syntax diagnostics byte-compatible.  The configurable parser
           remains authoritative for acceptance: if PUC unexpectedly accepts
           input that our builtin profile rejected, report the frontend
           mismatch instead of silently accepting it.  This is an instance
           property, not a profile-name check. */
        if (lf_profile_native_diag(j->profile)) {
            OneReader mr={j->buf,j->len,0};
            ZIO z;
            luaZ_init(L,&z,one_reader,&mr);
            int nst=(int)luaD_protectedparser(L,&z,j->chunkname,j->mode);
            if (nst != LUA_OK) luaD_throw(L,cast(TStatus,nst));
            /* Native accepted: remove its closure and surface our failure. */
            L->top.p--;
        }
        if (e.kind == LF_ERROR_INCOMPLETE) {
            if (e.span.line_start)
                lua_pushfstring(L, "%s:%d:%d: %s <eof>", j->chunkname,
                                (int)e.span.line_start, (int)e.span.col_start,
                                e.message[0] ? e.message : "incomplete input");
            else
                lua_pushfstring(L, "%s <eof>",
                                e.message[0] ? e.message : "incomplete input");
        }
        else if (e.span.line_start)
            lua_pushfstring(L,"%s:%d:%d: %s",j->chunkname,
                            (int)e.span.line_start,(int)e.span.col_start,e.message);
        else
            lua_pushstring(L,e.message[0]?e.message:"frontend parse failed");
        luaD_throw(L,LUA_ERRSYNTAX);
    }
    if (getenv("LF_TRACE") && j->len > 1000) fprintf(stderr,"[lf] compile ast\n");
    int st=compile_ast_raw(L,ast,j->chunkname,&e);
    if (getenv("LF_TRACE") && j->len > 1000) fprintf(stderr,"[lf] compile done st=%d top=%td\n",st,L->top.p-L->stack.p);
    lf_ast_free(ast);
    if (getenv("LF_TRACE") && j->len > 1000) fprintf(stderr,"[lf] ast freed\n");
    if (st != LUA_OK) luaD_throw(L,cast(TStatus,st));
}

static int source_compiler_hook(lua_State *L, lua_Reader reader, void *data,
                                const char *chunkname, const char *mode,
                                const lua_SourceInfo *info, void *ud) {
    HookJob job={0};
    job.registry=(LF_ProfileRegistry *)ud;
    job.info=info;
    job.reader=reader; job.reader_data=data;
    job.chunkname=chunkname?chunkname:"?"; job.mode=mode;
    ptrdiff_t oldtop=savestack(L,L->top.p);
    TStatus st=luaD_pcall(L,compile_hook_job,&job,oldtop,L->errfunc);
    free(job.buf);
    return (int)st;
}

void lf_lua_install_profile(lua_State *L, LF_Profile *profile) {
    /* Legacy single-profile API: a registry whose resolver always returns
       the given profile.  The profile stays owned by the caller. */
    if (profile == NULL) {
        lua_setsourcecompiler(L, NULL, NULL);
        return;
    }
    LF_ProfileRegistry *r = registry_new(L);  /* userdata on top */
    r->override_all = profile;
    /* no syntax preload for the legacy API: no registry-managed names */
    registry_store_ref(L);
    lua_setsourcecompiler(L, source_compiler_hook, r);
    lua_pop(L, 1);
}

void lf_lua_uninstall_profile(lua_State *L) {
    lua_setsourcecompiler(L,NULL,NULL);
}

