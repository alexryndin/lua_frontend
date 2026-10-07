#include "luafront.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct LF_CanonTokens {
    LF_CanonToken *v;
    size_t n, cap;
};

typedef struct {
    LF_CanonTokens *out;
    LF_Error *err;
    int failed;
} Emit;

static char *dupn(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) abort();
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static void fail(Emit *e, const LF_Ast *a, const char *msg) {
    if (e->failed) return;
    e->failed = 1;
    if (e->err) {
        e->err->span = lf_ast_span(a);
        snprintf(e->err->message, sizeof(e->err->message),
                 "canonical token lowering: %s", msg);
    }
}

static void pushn(Emit *e, LF_CanonTokenKind kind, const char *text, size_t text_len, LF_Span sp) {
    if (e->failed) return;
    if (e->out->n == e->out->cap) {
        size_t nc = e->out->cap ? e->out->cap * 2 : 128;
        LF_CanonToken *nv = (LF_CanonToken *)realloc(e->out->v, nc * sizeof(*nv));
        if (!nv) abort();
        e->out->v = nv;
        e->out->cap = nc;
    }
    LF_CanonToken *t = &e->out->v[e->out->n++];
    t->kind = kind;
    t->text = dupn(text ? text : "", text ? text_len : 0);
    t->text_len = text ? text_len : 0;
    t->span = sp;
}
static void push(Emit *e, LF_CanonTokenKind kind, const char *text, LF_Span sp) {
    pushn(e, kind, text, text ? strlen(text) : 0, sp);
}

static void lit(Emit *e, const LF_Ast *a, const char *s) {
    push(e, LF_CT_LITERAL, s, lf_ast_span(a));
}

static void lit_end(Emit *e, const LF_Ast *a, const char *s) {
    LF_Span sp=lf_ast_span(a);
    sp.byte_start=sp.byte_end;
    sp.line_start=sp.line_end;
    sp.col_start=sp.col_end;
    push(e, LF_CT_LITERAL, s, sp);
}

static void lit_origin(Emit *e,const LF_Ast *a,const char *s,size_t occurrence,int end_fallback) {
    LF_Span sp;
    if (lf_ast_literal_span(a,s,occurrence,&sp)) push(e,LF_CT_LITERAL,s,sp);
    else if (end_fallback) lit_end(e,a,s);
    else lit(e,a,s);
}

static void name_tok(Emit *e, const LF_Ast *a) {
    const char *s = lf_ast_text(a);
    if (!s) { fail(e, a, "Name without text"); return; }
    pushn(e, LF_CT_NAME, s, lf_ast_text_len(a), lf_ast_span(a));
}

static const LF_Ast *child(const LF_Ast *a, size_t i) {
    return lf_ast_child(a, i);
}

static int iskind(const LF_Ast *a, const char *k) {
    const char *ak = lf_ast_kind(a);
    return ak && strcmp(ak, k) == 0;
}

static void emit_expr(Emit *e, const LF_Ast *a);
static void emit_block(Emit *e, const LF_Ast *a);

static void emit_list(Emit *e, const LF_Ast *a, void (*fn)(Emit *, const LF_Ast *), const char *sep) {
    size_t n = lf_ast_child_count(a);
    for (size_t i = 0; i < n; i++) {
        if (i) lit(e, a, sep);
        fn(e, child(a, i));
    }
}

static void emit_attr(Emit *e, const LF_Ast *a) {
    if (!a || !iskind(a, "Attr") || lf_ast_child_count(a) != 1) {
        fail(e, a, "invalid Attr node"); return;
    }
    lit_origin(e,a,"<",0,0);
    name_tok(e, child(a, 0));
    lit_origin(e,a,">",0,1);
}

static void emit_declname(Emit *e, const LF_Ast *a) {
    if (!a || !iskind(a, "DeclName") || lf_ast_child_count(a) < 1) {
        fail(e, a, "invalid DeclName node"); return;
    }
    name_tok(e, child(a, 0));
    if (lf_ast_child_count(a) > 1) emit_attr(e, child(a, 1));
}

static void emit_decllist(Emit *e, const LF_Ast *a) {
    if (!a || !iskind(a, "DeclList")) { fail(e, a, "invalid DeclList node"); return; }
    size_t n = lf_ast_child_count(a), i = 0;
    if (i < n && iskind(child(a, i), "Attr")) {
        emit_attr(e, child(a, i++));
    }
    int first = 1;
    for (; i < n; i++) {
        if (!iskind(child(a, i), "DeclName")) { fail(e, child(a, i), "non-DeclName in DeclList"); return; }
        if (!first) lit(e, a, ",");
        emit_declname(e, child(a, i));
        first = 0;
    }
    if (first) fail(e, a, "empty declaration list");
}

static void emit_namelist(Emit *e, const LF_Ast *a) {
    if (!a || !iskind(a, "ParamList")) { fail(e, a, "invalid name list"); return; }
    size_t n = lf_ast_child_count(a);
    for (size_t i = 0; i < n; i++) {
        if (i) lit(e, a, ",");
        name_tok(e, child(a, i));
    }
}

static void emit_exprlist(Emit *e, const LF_Ast *a) {
    if (!a || !iskind(a, "ExprList")) { fail(e, a, "invalid expression list"); return; }
    emit_list(e, a, emit_expr, ",");
}

static void emit_varlist(Emit *e, const LF_Ast *a) {
    if (!a || !iskind(a, "VarList")) { fail(e, a, "invalid variable list"); return; }
    emit_list(e, a, emit_expr, ",");
}

static void emit_vararg_param(Emit *e, const LF_Ast *a) {
    lit(e, a, "...");
    if (lf_ast_child_count(a) > 0) {
        name_tok(e, child(a, 0));
    }
}

static void emit_params(Emit *e, const LF_Ast *a) {
    if (!a) return;
    if (iskind(a, "ParamList")) { emit_namelist(e, a); return; }
    if (!iskind(a, "Params")) { fail(e, a, "invalid Params node"); return; }
    int wrote = 0;
    for (size_t i = 0; i < lf_ast_child_count(a); i++) {
        const LF_Ast *x = child(a, i);
        if (iskind(x, "ParamList")) {
            if (lf_ast_child_count(x)) {
                if (wrote) lit(e, a, ",");
                emit_namelist(e, x);
                wrote = 1;
            }
        }
        else if (iskind(x, "Vararg")) {
            if (wrote) lit(e, a, ",");
            emit_vararg_param(e, x);
            wrote = 1;
        }
        else { fail(e, x, "invalid function parameter node"); return; }
    }
}

static void emit_funcbody(Emit *e, const LF_Ast *a) {
    if (!a || !iskind(a, "FunctionBody")) { fail(e, a, "invalid FunctionBody"); return; }
    const LF_Ast *params = NULL, *body = NULL;
    for (size_t i = 0; i < lf_ast_child_count(a); i++) {
        const LF_Ast *x = child(a, i);
        if (iskind(x, "Params") || iskind(x, "ParamList")) params = x;
        else if (iskind(x, "Block")) body = x;
    }
    if (!body) { fail(e, a, "FunctionBody without Block"); return; }
    lit_origin(e,a,"(",0,0);
    if (params) emit_params(e, params);
    lit_origin(e,a,")",0,1);
    emit_block(e, body);
    lit_origin(e,a,"end",0,1);
}

static void emit_funcname(Emit *e, const LF_Ast *a) {
    if (!a || !iskind(a, "FuncName") || lf_ast_child_count(a) < 1) {
        fail(e, a, "invalid FuncName"); return;
    }
    name_tok(e, child(a, 0));
    for (size_t i = 1; i < lf_ast_child_count(a); i++) {
        const LF_Ast *x = child(a, i);
        if (iskind(x, "FuncField")) lit(e, x, ".");
        else if (iskind(x, "FuncMethod")) lit(e, x, ":");
        else { fail(e, x, "invalid function-name component"); return; }
        if (lf_ast_child_count(x) != 1) { fail(e, x, "invalid function-name component"); return; }
        name_tok(e, child(x, 0));
    }
}

static int prefix_ok(const LF_Ast *a) {
    return iskind(a,"Name") || iskind(a,"Field") || iskind(a,"Index") ||
           iskind(a,"Call") || iskind(a,"MethodCall") || iskind(a,"Paren");
}

static void emit_prefix(Emit *e, const LF_Ast *a) {
    if (prefix_ok(a)) emit_expr(e, a);
    else {
        lit_origin(e,a,"(",0,0); emit_expr(e, a); lit_origin(e,a,")",0,1);
    }
}

static void emit_call_args(Emit *e, const LF_Ast *owner, const LF_Ast *a) {
    lit_origin(e, owner, "(", 0, 0);
    if (a) {
        if (iskind(a, "ExprList")) emit_exprlist(e, a);
        else emit_expr(e, a);
    }
    lit_origin(e, owner, ")", 0, 1);
}

static void emit_table(Emit *e, const LF_Ast *a) {
    lit(e, a, "{");
    const LF_Ast *list = lf_ast_child_count(a) ? child(a,0) : NULL;
    if (list) {
        if (!iskind(list, "List")) { fail(e, list, "Table child is not a field list"); return; }
        for (size_t i = 0; i < lf_ast_child_count(list); i++) {
            const LF_Ast *f = child(list, i);
            if (i) lit(e, f, ",");
            if (iskind(f, "NamedField")) {
                if (lf_ast_child_count(f) != 2) { fail(e,f,"invalid NamedField"); return; }
                name_tok(e, child(f,0)); lit(e,f,"="); emit_expr(e,child(f,1));
            }
            else if (iskind(f, "KeyedField")) {
                if (lf_ast_child_count(f) != 2) { fail(e,f,"invalid KeyedField"); return; }
                lit(e,f,"["); emit_expr(e,child(f,0)); lit(e,f,"]"); lit(e,f,"="); emit_expr(e,child(f,1));
            }
            else if (iskind(f, "ValueField")) {
                if (lf_ast_child_count(f) != 1) { fail(e,f,"invalid ValueField"); return; }
                emit_expr(e,child(f,0));
            }
            else { fail(e,f,"unknown table field kind"); return; }
        }
    }
    lit(e, a, "}");
}

static int binary_prec(const char *op) {
    if (!op) return 0;
    if (strcmp(op,"or")==0) return 1;
    if (strcmp(op,"and")==0) return 2;
    if (strcmp(op,"<")==0 || strcmp(op,"<=")==0 || strcmp(op,">")==0 ||
        strcmp(op,">=")==0 || strcmp(op,"==")==0 || strcmp(op,"~=")==0) return 3;
    if (strcmp(op,"|")==0) return 4;
    if (strcmp(op,"~")==0) return 5;
    if (strcmp(op,"&")==0) return 6;
    if (strcmp(op,"<<")==0 || strcmp(op,">>")==0) return 7;
    if (strcmp(op,"..")==0) return 8;
    if (strcmp(op,"+")==0 || strcmp(op,"-")==0) return 9;
    if (strcmp(op,"*")==0 || strcmp(op,"/")==0 || strcmp(op,"//")==0 || strcmp(op,"%")==0) return 10;
    if (strcmp(op,"^")==0) return 12;
    return 0;
}

static int binary_right_assoc(const char *op) {
    return op && (strcmp(op,"^")==0 || strcmp(op,"..")==0);
}

/*
 * Emit exactly the grouping represented by the canonical AST, but do not
 * manufacture a parenthesis layer for every binary node.  'side' is -1 for
 * a left child, +1 for a right child, 0 outside a binary parent.
 */
static void emit_expr_ctx(Emit *e, const LF_Ast *a,
                          int parent_prec, int parent_right_assoc, int side) {
    if (!a || e->failed) return;
    const char *k = lf_ast_kind(a);
    size_t n = lf_ast_child_count(a);

    if (strcmp(k,"Binary")==0) {
        const char *op = lf_ast_text(a);
        int p = binary_prec(op);
        if (n != 2 || p == 0) { fail(e,a,"invalid Binary"); return; }
        int paren = (p < parent_prec) ||
                    (p == parent_prec && side != 0 &&
                     ((!parent_right_assoc && side > 0) ||
                      ( parent_right_assoc && side < 0)));
        if (paren) lit(e,a,"(");
        int right = binary_right_assoc(op);
        emit_expr_ctx(e,child(a,0),p,right,-1);
        lit_origin(e,a,op,0,0);
        emit_expr_ctx(e,child(a,1),p,right,+1);
        if (paren) lit(e,a,")");
        return;
    }

    if (strcmp(k,"Unary")==0) {
        if (n!=1 || !lf_ast_text(a)) { fail(e,a,"invalid Unary"); return; }
        const int p = 11;
        int paren = p < parent_prec;
        if (paren) lit(e,a,"(");
        lit_origin(e,a,lf_ast_text(a),0,0);
        emit_expr_ctx(e,child(a,0),p,0,+1);
        if (paren) lit(e,a,")");
        return;
    }

    /* Non-operator expressions are primary/postfix expressions. */
    if (strcmp(k,"Name")==0) { name_tok(e,a); }
    else if (strcmp(k,"Number")==0) { pushn(e,LF_CT_NUMBER,lf_ast_text(a),lf_ast_text_len(a),lf_ast_span(a)); }
    else if (strcmp(k,"String")==0) { pushn(e,LF_CT_STRING,lf_ast_text(a),lf_ast_text_len(a),lf_ast_span(a)); }
    else if (strcmp(k,"Nil")==0) { lit(e,a,"nil"); }
    else if (strcmp(k,"Bool")==0) { lit(e,a,lf_ast_text(a)); }
    else if (strcmp(k,"Vararg")==0) { lit(e,a,"..."); }
    else if (strcmp(k,"Paren")==0) {
        if (n!=1) { fail(e,a,"invalid Paren"); return; }
        /* Explicit Paren is semantic in Lua: it can force a single result. */
        lit(e,a,"("); emit_expr_ctx(e,child(a,0),0,0,0); lit(e,a,")");
    }
    else if (strcmp(k,"Index")==0) {
        if (n!=2) { fail(e,a,"invalid Index"); return; }
        emit_prefix(e,child(a,0)); lit(e,a,"["); emit_expr_ctx(e,child(a,1),0,0,0); lit(e,a,"]");
    }
    else if (strcmp(k,"Field")==0) {
        if (n!=2) { fail(e,a,"invalid Field"); return; }
        emit_prefix(e,child(a,0)); lit(e,a,"."); name_tok(e,child(a,1));
    }
    else if (strcmp(k,"Call")==0) {
        if (n<1 || n>2) { fail(e,a,"invalid Call"); return; }
        emit_prefix(e,child(a,0)); emit_call_args(e,a,n==2?child(a,1):NULL);
    }
    else if (strcmp(k,"MethodCall")==0) {
        if (n<2 || n>3) { fail(e,a,"invalid MethodCall"); return; }
        emit_prefix(e,child(a,0)); lit(e,a,":"); name_tok(e,child(a,1)); emit_call_args(e,a,n==3?child(a,2):NULL);
    }
    else if (strcmp(k,"FunctionExpr")==0) {
        if (n!=1) { fail(e,a,"invalid FunctionExpr"); return; }
        lit_origin(e,a,"function",0,0); emit_funcbody(e,child(a,0));
    }
    else if (strcmp(k,"Table")==0) emit_table(e,a);
    else {
        char b[160]; snprintf(b,sizeof(b),"unknown expression node '%s'",k?k:"<null>"); fail(e,a,b);
    }
}

static void emit_expr(Emit *e, const LF_Ast *a) {
    emit_expr_ctx(e,a,0,0,0);
}

static void emit_statement(Emit *e, const LF_Ast *a) {
    if (!a || e->failed) return;
    const char *k = lf_ast_kind(a);
    size_t n = lf_ast_child_count(a);
    if (strcmp(k,"Empty")==0) { lit_end(e,a,";"); return; }
    if (strcmp(k,"Assign")==0) {
        if (n!=2) { fail(e,a,"invalid Assign"); return; }
        emit_varlist(e,child(a,0)); lit_origin(e,a,"=",0,0); emit_exprlist(e,child(a,1)); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"Local")==0 || strcmp(k,"Global")==0) {
        if (n<1 || n>2) { fail(e,a,"invalid declaration"); return; }
        lit(e,a,strcmp(k,"Local")==0?"local":"global"); emit_decllist(e,child(a,0));
        if (n==2 && iskind(child(a,1),"ExprList") && lf_ast_child_count(child(a,1))>0) {
            lit_origin(e,a,"=",0,0); emit_exprlist(e,child(a,1));
        }
        lit_end(e,a,";"); return;
    }
    if (strcmp(k,"GlobalAll")==0) {
        lit_origin(e,a,"global",0,0);
        if (n) emit_attr(e,child(a,0));
        lit_origin(e,a,"*",0,0); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"FunctionDecl")==0) {
        if (n!=2) { fail(e,a,"invalid FunctionDecl"); return; }
        lit_origin(e,a,"function",0,0); emit_funcname(e,child(a,0)); emit_funcbody(e,child(a,1)); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"LocalFunction")==0 || strcmp(k,"GlobalFunction")==0) {
        if (n!=2) { fail(e,a,"invalid scoped function declaration"); return; }
        lit(e,a,strcmp(k,"LocalFunction")==0?"local":"global"); lit_origin(e,a,"function",0,0); name_tok(e,child(a,0)); emit_funcbody(e,child(a,1)); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"Do")==0) {
        if (n!=1) { fail(e,a,"invalid Do"); return; }
        lit_origin(e,a,"do",0,0); emit_block(e,child(a,0)); lit_origin(e,a,"end",0,1); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"While")==0) {
        if (n!=2) { fail(e,a,"invalid While"); return; }
        lit_origin(e,a,"while",0,0); emit_expr(e,child(a,0)); lit_origin(e,a,"do",0,0); emit_block(e,child(a,1)); lit_origin(e,a,"end",0,1); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"Repeat")==0) {
        if (n!=2) { fail(e,a,"invalid Repeat"); return; }
        lit_origin(e,a,"repeat",0,0); emit_block(e,child(a,0)); lit_origin(e,a,"until",0,0); emit_expr(e,child(a,1)); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"If")==0) {
        if (n<2) { fail(e,a,"invalid If"); return; }
        lit_origin(e,a,"if",0,0); emit_expr(e,child(a,0)); lit_origin(e,a,"then",0,0); emit_block(e,child(a,1));
        for (size_t i=2;i<n;i++) {
            const LF_Ast *b=child(a,i);
            if (iskind(b,"ElseIf")) {
                if (lf_ast_child_count(b)!=2) { fail(e,b,"invalid ElseIf"); return; }
                lit_origin(e,b,"elseif",0,0); emit_expr(e,child(b,0)); lit_origin(e,b,"then",0,0); emit_block(e,child(b,1));
            }
            else if (iskind(b,"Else")) {
                if (lf_ast_child_count(b)!=1) { fail(e,b,"invalid Else"); return; }
                lit_origin(e,b,"else",0,0); emit_block(e,child(b,0));
            }
            else { fail(e,b,"invalid If branch"); return; }
        }
        lit_origin(e,a,"end",0,1); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"NumericFor")==0) {
        if (n!=4 && n!=5) { fail(e,a,"invalid NumericFor"); return; }
        lit_origin(e,a,"for",0,0); name_tok(e,child(a,0)); lit_origin(e,a,"=",0,0); emit_expr(e,child(a,1)); lit(e,a,","); emit_expr(e,child(a,2));
        size_t bi=3;
        if (n==5) { lit(e,a,","); emit_expr(e,child(a,3)); bi=4; }
        lit_origin(e,a,"do",0,0); emit_block(e,child(a,bi)); lit_origin(e,a,"end",0,1); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"GenericFor")==0) {
        if (n!=3) { fail(e,a,"invalid GenericFor"); return; }
        lit_origin(e,a,"for",0,0); emit_namelist(e,child(a,0)); lit_origin(e,a,"in",0,0); emit_exprlist(e,child(a,1)); lit_origin(e,a,"do",0,0); emit_block(e,child(a,2)); lit_origin(e,a,"end",0,1); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"Return")==0) {
        lit_origin(e,a,"return",0,0);
        if (n==1 && iskind(child(a,0),"ExprList") && lf_ast_child_count(child(a,0))>0) emit_exprlist(e,child(a,0));
        else if (n>1) { fail(e,a,"invalid Return"); return; }
        lit_end(e,a,";"); return;
    }
    if (strcmp(k,"Break")==0) { lit_origin(e,a,"break",0,0); lit_end(e,a,";"); return; }
    if (strcmp(k,"Goto")==0) {
        if (n!=1) { fail(e,a,"invalid Goto"); return; }
        lit_origin(e,a,"goto",0,0); name_tok(e,child(a,0)); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"Label")==0) {
        if (n!=1) { fail(e,a,"invalid Label"); return; }
        lit_origin(e,a,"::",0,0); name_tok(e,child(a,0)); lit_origin(e,a,"::",0,0); lit_end(e,a,";"); return;
    }
    if (strcmp(k,"Call")==0 || strcmp(k,"MethodCall")==0) {
        emit_expr(e,a); lit_end(e,a,";"); return;
    }
    {
        char b[160]; snprintf(b,sizeof(b),"unknown statement node '%s'",k?k:"<null>"); fail(e,a,b);
    }
}

static void emit_block(Emit *e, const LF_Ast *a) {
    if (!a || !iskind(a,"Block")) { fail(e,a,"invalid Block"); return; }
    for (size_t i=0;i<lf_ast_child_count(a);i++) emit_statement(e,child(a,i));
}

LF_CanonTokens *lf_ast_to_canonical_tokens(const LF_Ast *ast, LF_Error *err) {
    if (!ast || !iskind(ast,"Chunk") || lf_ast_child_count(ast)!=1 || !iskind(child(ast,0),"Block")) {
        if (err) {
            err->span = ast ? lf_ast_span(ast) : (LF_Span){0};
            snprintf(err->message,sizeof(err->message),"canonical token lowering: expected Chunk(Block)");
        }
        return NULL;
    }
    LF_CanonTokens *t=(LF_CanonTokens*)calloc(1,sizeof(*t));
    if(!t) abort();
    Emit e={t,err,0};
    emit_block(&e,child(ast,0));
    if (!e.failed) {
        LF_Span eofsp=lf_ast_span(ast);
        eofsp.byte_start=eofsp.byte_end;
        eofsp.line_start=eofsp.line_end;
        eofsp.col_start=eofsp.col_end;
        push(&e,LF_CT_EOF,"<eof>",eofsp);
    }
    if (e.failed) { lf_canonical_tokens_free(t); return NULL; }
    return t;
}

void lf_canonical_tokens_free(LF_CanonTokens *t) {
    if(!t) return;
    for(size_t i=0;i<t->n;i++) free((char*)t->v[i].text);
    free(t->v); free(t);
}
size_t lf_canonical_token_count(const LF_CanonTokens *t){return t?t->n:0;}
const LF_CanonToken *lf_canonical_token_at(const LF_CanonTokens *t,size_t i){return (t&&i<t->n)?&t->v[i]:NULL;}

void lf_canonical_tokens_dump(const LF_CanonTokens *t, FILE *out) {
    if(!t) return;
    for(size_t i=0;i<t->n;i++) {
        const LF_CanonToken *x=&t->v[i];
        const char *k=x->kind==LF_CT_EOF?"EOF":x->kind==LF_CT_LITERAL?"LIT":x->kind==LF_CT_NAME?"NAME":x->kind==LF_CT_NUMBER?"NUMBER":"STRING";
        fprintf(out,"%s(",k);
        if (x->text) {
            for (size_t j=0;j<x->text_len;j++) {
                unsigned char c=(unsigned char)x->text[j];
                if (c=='\\') fputs("\\\\",out);
                else if (c=='\0') fputs("\\0",out);
                else if (c=='\n') fputs("\\n",out);
                else if (c=='\r') fputs("\\r",out);
                else if (c=='\t') fputs("\\t",out);
                else if (c<0x20 || c==0x7f) fprintf(out,"\\x%02X",c);
                else fputc(c,out);
            }
        }
        fprintf(out,") @%u:%u\n",x->span.line_start,x->span.col_start);
    }
}
