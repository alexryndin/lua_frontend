#include "luafront_lua.h"

/* PUC Lua internal compiler interface. */
#include "ldo.h"
#include "llex.h"
#include "lobject.h"
#include "lzio.h"

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
    LF_Profile *profile;
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

    LF_Error e={0};
    if (getenv("LF_TRACE") && j->len > 1000) fprintf(stderr,"[lf] parse len=%zu\n",j->len);
    LF_Ast *ast=lf_parse(j->profile,j->chunkname,j->buf,j->len,&e);
    if (getenv("LF_TRACE") && j->len > 1000) fprintf(stderr,"[lf] parse done ast=%p\n",(void*)ast);
    if (!ast) {
        /* For the reference Lua profile, keep PUC's user-visible syntax
           diagnostics byte-compatible.  The configurable parser remains
           authoritative for acceptance: if PUC unexpectedly accepts input
           that our lua55 profile rejected, report the frontend mismatch
           instead of silently accepting it. */
        const char *pname=lf_profile_name(j->profile);
        if (pname && strcmp(pname,"lua55")==0) {
            OneReader mr={j->buf,j->len,0};
            ZIO z;
            luaZ_init(L,&z,one_reader,&mr);
            int nst=(int)luaD_protectedparser(L,&z,j->chunkname,j->mode);
            if (nst != LUA_OK) luaD_throw(L,cast(TStatus,nst));
            /* Native accepted: remove its closure and surface our failure. */
            L->top.p--;
        }
        if (e.span.line_start)
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
                                void *ud) {
    HookJob job={0};
    job.profile=(LF_Profile *)ud;
    job.reader=reader; job.reader_data=data;
    job.chunkname=chunkname?chunkname:"?"; job.mode=mode;
    ptrdiff_t oldtop=savestack(L,L->top.p);
    TStatus st=luaD_pcall(L,compile_hook_job,&job,oldtop,L->errfunc);
    free(job.buf);
    return (int)st;
}

void lf_lua_install_profile(lua_State *L, LF_Profile *profile) {
    lua_setsourcecompiler(L, profile ? source_compiler_hook : NULL, profile);
}

void lf_lua_uninstall_profile(lua_State *L) {
    lua_setsourcecompiler(L,NULL,NULL);
}

