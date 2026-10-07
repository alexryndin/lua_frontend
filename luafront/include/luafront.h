#ifndef LUAFRONT_H
#define LUAFRONT_H

#include <stddef.h>
#include <stdio.h>

typedef struct LF_Profile LF_Profile;
typedef struct LF_Ast LF_Ast;
typedef struct LF_CanonTokens LF_CanonTokens;

typedef struct {
    const char *file;
    size_t byte_start, byte_end;
    unsigned line_start, col_start, line_end, col_end;
} LF_Span;

typedef enum {
    LF_ERROR_NONE = 0,
    LF_ERROR_IO,
    LF_ERROR_PROFILE,
    LF_ERROR_SYNTAX,
    LF_ERROR_INCOMPLETE,
    LF_ERROR_CANONICAL,
    LF_ERROR_COMPILER
} LF_ErrorKind;

typedef struct {
    char message[512];
    LF_Span span;
    LF_ErrorKind kind;
} LF_Error;

typedef enum {
    LF_CT_EOF,
    LF_CT_LITERAL,
    LF_CT_NAME,
    LF_CT_NUMBER,
    LF_CT_STRING
} LF_CanonTokenKind;

typedef struct {
    LF_CanonTokenKind kind;
    const char *text;
    size_t text_len;
    LF_Span span;
} LF_CanonToken;

LF_Profile *lf_profile_load(const char *path, LF_Error *err);
LF_Profile *lf_profile_load_memory(const char *source_name, const char *source,
                                   size_t source_len, LF_Error *err);
LF_Profile *lf_profile_builtin_lua55(LF_Error *err);
/* Mark a profile as a trusted builtin instance (PUC-native diagnostics).
   Not part of the profile DSL; intended for programmatic builtin profiles. */
void lf_profile_set_native_diag(LF_Profile *p);
/* Instance property of trusted builtin profiles (PUC-native diagnostics). */
int lf_profile_native_diag(const LF_Profile *p);
/* Resolve the active profile in this order: explicit path, LUA_SYNTAX,
   ./.lua-syntax, XDG/HOME user config, /etc/lua/syntax, builtin Lua 5.5.
   With noenv!=0, environment-based selections/config paths are skipped. */
LF_Profile *lf_profile_discover(const char *explicit_path, int noenv,
                                LF_Error *err);
void lf_profile_free(LF_Profile *profile);

LF_Ast *lf_parse(LF_Profile *profile, const char *source_name,
                 const char *source, size_t source_len, LF_Error *err);
/* Parse using a named profile rule instead of the default `chunk` root.
   This is primarily used by profile-aware tooling and the REPL. */
LF_Ast *lf_parse_rule(LF_Profile *profile, const char *rule_name,
                      const char *source_name, const char *source,
                      size_t source_len, LF_Error *err);
int lf_profile_has_rule(const LF_Profile *profile, const char *rule_name);
void lf_ast_free(LF_Ast *ast);
void lf_ast_dump(const LF_Ast *ast, FILE *out);

/* Read-only AST introspection for compilers/tooling. */
const char *lf_ast_kind(const LF_Ast *ast);
const char *lf_ast_text(const LF_Ast *ast);
size_t lf_ast_text_len(const LF_Ast *ast);
LF_Span lf_ast_span(const LF_Ast *ast);
size_t lf_ast_child_count(const LF_Ast *ast);
const LF_Ast *lf_ast_child(const LF_Ast *ast, size_t index);
const char *lf_ast_child_field(const LF_Ast *ast, size_t index);
/* Source span of a syntax literal consumed directly by the production that
   created this semantic node. `occurrence` is zero-based.  Literal origins
   are metadata only; they do not affect canonical AST semantics. */
int lf_ast_literal_span(const LF_Ast *ast, const char *literal,
                        size_t occurrence, LF_Span *out);

/*
** Lower the canonical AST to standard Lua lexical tokens.  This is an
** internal compiler representation, not source-to-source translation.
** Token spans still refer to the original dialect source.
*/
LF_CanonTokens *lf_ast_to_canonical_tokens(const LF_Ast *ast, LF_Error *err);
void lf_canonical_tokens_free(LF_CanonTokens *tokens);
size_t lf_canonical_token_count(const LF_CanonTokens *tokens);
const LF_CanonToken *lf_canonical_token_at(const LF_CanonTokens *tokens,
                                           size_t index);
void lf_canonical_tokens_dump(const LF_CanonTokens *tokens, FILE *out);

const char *lf_profile_name(const LF_Profile *profile);
const char *lf_profile_version(const LF_Profile *profile);
size_t lf_profile_rule_count(const LF_Profile *profile);

#endif
