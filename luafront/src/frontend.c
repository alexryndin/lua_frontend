#include "luafront.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LF_INIT_CAP 16
#define LF_MAX_RECURSION 8192

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "luafront: out of memory\n"); abort(); }
    return p;
}
static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "luafront: out of memory\n"); abort(); }
    return q;
}
static char *xstrndup(const char *s, size_t n) {
    char *p = (char *)xmalloc(n + 1);
    memcpy(p, s, n); p[n] = 0; return p;
}
static char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

static void seterrv(LF_Error *e, LF_ErrorKind kind, LF_Span span,
                    const char *fmt, va_list ap) {
    if (!e) return;
    e->span = span;
    e->kind = kind;
    vsnprintf(e->message, sizeof(e->message), fmt, ap);
}

static void seterrk(LF_Error *e, LF_ErrorKind kind, LF_Span span,
                    const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    seterrv(e, kind, span, fmt, ap);
    va_end(ap);
}

static void seterr(LF_Error *e, LF_Span span, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    seterrv(e, LF_ERROR_SYNTAX, span, fmt, ap);
    va_end(ap);
}

/* ---------- grammar IR ---------- */

typedef enum {
    GX_SEQ, GX_ALT, GX_OPT, GX_REP, GX_REF, GX_LIT, GX_CLASS, GX_CAPTURE
} GKind;

typedef struct GExpr GExpr;
struct GExpr {
    GKind kind;
    char *text;
    GExpr **items;
    size_t nitems;
};

typedef enum { AX_REF, AX_STRING, AX_CALL, AX_NULL } AKind;
typedef struct AExpr AExpr;
struct AExpr {
    AKind kind;
    char *text;
    AExpr **args;
    size_t nargs;
};

typedef struct {
    char *name;
    GExpr *expr;
    AExpr *action;
} Rule;

typedef struct {
    char *open;
    char *close;
} BlockComment;

struct LF_Profile {
    char *name;
    char *version;
    Rule *rules;
    size_t nrules, caprules;
    char **literals;
    size_t nliterals, capliterals;
    char **contextuals;
    size_t ncontextuals, capcontextuals;
    /* configurable comment trivia; when comments_explicit is zero the lexer
       falls back to the built-in Lua comment set ('--' line + '--' long) */
    int comments_explicit;
    char **line_comments;
    size_t nline_comments, capline_comments;
    BlockComment *block_comments;
    size_t nblock_comments, capblock_comments;
    char **long_markers;   /* lua_long_comment markers (marker + long bracket) */
    size_t nlong_markers, caplong_markers;
};

static GExpr *gnew(GKind k) {
    GExpr *g=(GExpr*)calloc(1,sizeof(*g)); if(!g) abort(); g->kind=k; return g;
}
static void gadd(GExpr *g, GExpr *x) {
    if (g->nitems % LF_INIT_CAP == 0) g->items=(GExpr**)xrealloc(g->items,(g->nitems+LF_INIT_CAP)*sizeof(*g->items));
    g->items[g->nitems++]=x;
}
static void gfree(GExpr *g) {
    if(!g) return;
    free(g->text);
    for(size_t i=0;i<g->nitems;i++) gfree(g->items[i]);
    free(g->items);
    free(g);
}
static AExpr *anew(AKind k) { AExpr *a=(AExpr*)calloc(1,sizeof(*a)); if(!a) abort(); a->kind=k; return a; }
static void aadd(AExpr *a,AExpr*x){ if(a->nargs%LF_INIT_CAP==0)a->args=(AExpr**)xrealloc(a->args,(a->nargs+LF_INIT_CAP)*sizeof(*a->args)); a->args[a->nargs++]=x; }
static void afree(AExpr*a){ if(!a)return; free(a->text); for(size_t i=0;i<a->nargs;i++)afree(a->args[i]); free(a->args); free(a); }

static Rule *find_rule(LF_Profile *p,const char *name){ for(size_t i=0;i<p->nrules;i++) if(strcmp(p->rules[i].name,name)==0)return &p->rules[i]; return NULL; }

static void add_literal(LF_Profile *p, const char *s) {
    for(size_t i=0;i<p->nliterals;i++) if(strcmp(p->literals[i],s)==0) return;
    if(p->nliterals==p->capliterals){p->capliterals=p->capliterals?p->capliterals*2:32;p->literals=(char**)xrealloc(p->literals,p->capliterals*sizeof(*p->literals));}
    p->literals[p->nliterals++]=xstrdup(s);
}
static void collect_literals(LF_Profile *p,GExpr*g){ if(!g)return; if(g->kind==GX_LIT)add_literal(p,g->text); for(size_t i=0;i<g->nitems;i++)collect_literals(p,g->items[i]); }
static void add_contextual(LF_Profile *p, const char *s) {
    for(size_t i=0;i<p->ncontextuals;i++) if(strcmp(p->contextuals[i],s)==0) return;
    if(p->ncontextuals==p->capcontextuals){p->capcontextuals=p->capcontextuals?p->capcontextuals*2:8;p->contextuals=(char**)xrealloc(p->contextuals,p->capcontextuals*sizeof(*p->contextuals));}
    p->contextuals[p->ncontextuals++]=xstrdup(s);
}
static int is_contextual(const LF_Profile *p, const char *s) {
    for(size_t i=0;i<p->ncontextuals;i++) if(strcmp(p->contextuals[i],s)==0) return 1;
    return 0;
}

static void add_line_comment(LF_Profile *p, const char *s) {
    for(size_t i=0;i<p->nline_comments;i++) if(strcmp(p->line_comments[i],s)==0) return;
    if(p->nline_comments==p->capline_comments){p->capline_comments=p->capline_comments?p->capline_comments*2:8;p->line_comments=(char**)xrealloc(p->line_comments,p->capline_comments*sizeof(*p->line_comments));}
    p->line_comments[p->nline_comments++]=xstrdup(s);
}
static void add_block_comment(LF_Profile *p, const char *open, const char *close) {
    for(size_t i=0;i<p->nblock_comments;i++) if(strcmp(p->block_comments[i].open,open)==0) return;
    if(p->nblock_comments==p->capblock_comments){p->capblock_comments=p->capblock_comments?p->capblock_comments*2:8;p->block_comments=(BlockComment*)xrealloc(p->block_comments,p->capblock_comments*sizeof(*p->block_comments));}
    p->block_comments[p->nblock_comments].open=xstrdup(open);
    p->block_comments[p->nblock_comments].close=xstrdup(close);
    p->nblock_comments++;
}
static void add_long_marker(LF_Profile *p, const char *s) {
    for(size_t i=0;i<p->nlong_markers;i++) if(strcmp(p->long_markers[i],s)==0) return;
    if(p->nlong_markers==p->caplong_markers){p->caplong_markers=p->caplong_markers?p->caplong_markers*2:8;p->long_markers=(char**)xrealloc(p->long_markers,p->caplong_markers*sizeof(*p->long_markers));}
    p->long_markers[p->nlong_markers++]=xstrdup(s);
}

/* ---------- profile lexer/parser ---------- */

typedef enum { PT_EOF, PT_ID, PT_STR, PT_EQ, PT_SEMI, PT_BAR, PT_LP, PT_RP, PT_LB, PT_RB, PT_LC, PT_RC, PT_COLON, PT_COMMA, PT_ARROW } PTKind;
typedef struct { PTKind k; char *text; size_t off; unsigned line,col; } PTok;
typedef struct { const char *s; size_t n,i; unsigned line,col; const char *file; PTok cur; LF_Error *err; int failed; } PScan;

static LF_Span pspan(PScan*q,size_t off,unsigned line,unsigned col){LF_Span s={q->file,off,off,line,col,line,col};return s;}
static void pskip(PScan*q){
    for(;;){ while(q->i<q->n && isspace((unsigned char)q->s[q->i])){ if(q->s[q->i]=='\n'){q->line++;q->col=1;} else q->col++; q->i++; }
        if(q->i+1<q->n && q->s[q->i]=='-'&&q->s[q->i+1]=='-'){ while(q->i<q->n&&q->s[q->i]!='\n'){q->i++;q->col++;} continue; }
        if(q->i<q->n && q->s[q->i]=='#'){ while(q->i<q->n&&q->s[q->i]!='\n'){q->i++;q->col++;} continue; }
        break;
    }
}
static char escchar(char c){switch(c){case'n':return'\n';case'r':return'\r';case't':return'\t';case'\\':return'\\';case'"':return'"';default:return c;}}
static void pnext(PScan*q){
    free(q->cur.text); q->cur.text=NULL; pskip(q); q->cur.off=q->i;q->cur.line=q->line;q->cur.col=q->col;
    if(q->i>=q->n){q->cur.k=PT_EOF;return;} char c=q->s[q->i++];q->col++;
    if(isalpha((unsigned char)c)||c=='_'){size_t st=q->i-1;while(q->i<q->n&&(isalnum((unsigned char)q->s[q->i])||q->s[q->i]=='_')){q->i++;q->col++;}q->cur.k=PT_ID;q->cur.text=xstrndup(q->s+st,q->i-st);return;}
    if(c=='"'){size_t cap=32,len=0;char *b=(char*)xmalloc(cap);while(q->i<q->n&&q->s[q->i]!='"'){char d=q->s[q->i++];q->col++;if(d=='\\'&&q->i<q->n){d=escchar(q->s[q->i++]);q->col++;}if(len+1>=cap){cap*=2;b=(char*)xrealloc(b,cap);}b[len++]=d;}if(q->i>=q->n){q->failed=1;seterr(q->err,pspan(q,q->cur.off,q->cur.line,q->cur.col),"unterminated string in syntax profile");free(b);q->cur.k=PT_EOF;return;}q->i++;q->col++;b[len]=0;q->cur.k=PT_STR;q->cur.text=b;return;}
    if(c=='='&&q->i<q->n&&q->s[q->i]=='>'){q->i++;q->col++;q->cur.k=PT_ARROW;return;}
    switch(c){case'=':q->cur.k=PT_EQ;break;case';':q->cur.k=PT_SEMI;break;case'|':q->cur.k=PT_BAR;break;case'(':q->cur.k=PT_LP;break;case')':q->cur.k=PT_RP;break;case'[':q->cur.k=PT_LB;break;case']':q->cur.k=PT_RB;break;case'{':q->cur.k=PT_LC;break;case'}':q->cur.k=PT_RC;break;case':':q->cur.k=PT_COLON;break;case',':q->cur.k=PT_COMMA;break;default:q->failed=1;seterr(q->err,pspan(q,q->cur.off,q->cur.line,q->cur.col),"unexpected character '%c' in syntax profile",c);q->cur.k=PT_EOF;}
}
static int paccept(PScan*q,PTKind k){if(q->cur.k==k){pnext(q);return 1;}return 0;}
static int pexpect(PScan*q,PTKind k,const char *what){if(paccept(q,k))return 1;q->failed=1;seterr(q->err,pspan(q,q->cur.off,q->cur.line,q->cur.col),"expected %s",what);return 0;}

static GExpr *parse_galt(PScan*q);
static GExpr *parse_gatom(PScan*q){
    GExpr *g=NULL;
    if(q->cur.k==PT_ID){char *t=xstrdup(q->cur.text);pnext(q);g=gnew((strcmp(t,"NAME")==0||strcmp(t,"NUMBER")==0||strcmp(t,"STRING")==0||strcmp(t,"EOF")==0)?GX_CLASS:GX_REF);g->text=t;}
    else if(q->cur.k==PT_STR){g=gnew(GX_LIT);g->text=xstrdup(q->cur.text);pnext(q);}
    else if(paccept(q,PT_LP)){g=parse_galt(q);if(!pexpect(q,PT_RP,"')'")){gfree(g);return NULL;}}
    else if(paccept(q,PT_LB)){GExpr *x=parse_galt(q);if(!pexpect(q,PT_RB,"']'")){gfree(x);return NULL;}g=gnew(GX_OPT);gadd(g,x);}
    else if(paccept(q,PT_LC)){GExpr *x=parse_galt(q);if(!pexpect(q,PT_RC,"'}'")){gfree(x);return NULL;}g=gnew(GX_REP);gadd(g,x);}
    else return NULL;
    if(paccept(q,PT_COLON)){if(q->cur.k!=PT_ID){q->failed=1;seterr(q->err,pspan(q,q->cur.off,q->cur.line,q->cur.col),"expected capture name");gfree(g);return NULL;}GExpr *c=gnew(GX_CAPTURE);c->text=xstrdup(q->cur.text);pnext(q);gadd(c,g);g=c;}
    return g;
}
static int atom_start(PTKind k){return k==PT_ID||k==PT_STR||k==PT_LP||k==PT_LB||k==PT_LC;}
static GExpr *parse_gseq(PScan*q){GExpr *s=gnew(GX_SEQ);while(atom_start(q->cur.k)){GExpr*a=parse_gatom(q);if(!a){gfree(s);return NULL;}gadd(s,a);}if(s->nitems==1){GExpr*x=s->items[0];free(s->items);free(s);return x;}return s;}
static GExpr *parse_galt(PScan*q){GExpr *first=parse_gseq(q);if(!first)return NULL;if(q->cur.k!=PT_BAR)return first;GExpr*a=gnew(GX_ALT);gadd(a,first);while(paccept(q,PT_BAR)){GExpr*x=parse_gseq(q);if(!x){gfree(a);return NULL;}gadd(a,x);}return a;}

static AExpr *parse_action(PScan*q){
    if(q->cur.k==PT_STR){AExpr*a=anew(AX_STRING);a->text=xstrdup(q->cur.text);pnext(q);return a;}
    if(q->cur.k!=PT_ID){q->failed=1;seterr(q->err,pspan(q,q->cur.off,q->cur.line,q->cur.col),"expected AST action expression");return NULL;}
    char *name=xstrdup(q->cur.text);pnext(q);
    if(strcmp(name,"null")==0 && q->cur.k!=PT_LP){free(name);return anew(AX_NULL);}
    if(!paccept(q,PT_LP)){AExpr*a=anew(AX_REF);a->text=name;return a;}
    AExpr*a=anew(AX_CALL);a->text=name;
    if(!paccept(q,PT_RP)){for(;;){AExpr*x=parse_action(q);if(!x){afree(a);return NULL;}aadd(a,x);if(paccept(q,PT_RP))break;if(!pexpect(q,PT_COMMA,"','")){afree(a);return NULL;}}}
    return a;
}

static int constructors_known(const char *s){
    static const char *k[]={
        "Chunk","Block","Do","Empty","Assign","Local","Global","GlobalAll",
        "FunctionDecl","LocalFunction","GlobalFunction","FunctionExpr","FunctionBody",
        "If","ElseIf","Else","While","Repeat","NumericFor","GenericFor","Return","Break","Goto","Label",
        "Call","MethodCall","Name","Index","Field","Nil","Bool","Number","String","Vararg",
        "Unary","Binary","Table","KeyedField","NamedField","ValueField","List","ParamList","Params","ExprList","VarList","Paren","Raw",
        "Attr","DeclName","DeclList","FuncName","FuncField","FuncMethod",
        /* semantic mapping helpers; these disappear while actions are evaluated */
        "Pass","Tail","Fold","Right","Postfix","IndexSuffix","FieldSuffix","CallSuffix","MethodSuffix"
    };
    for(size_t i=0;i<sizeof(k)/sizeof(k[0]);i++) {
        if(strcmp(k[i],s)==0) return 1;
    }
    return 0;
}
static int validate_action(AExpr*a,LF_Error*err,const char*file){if(!a)return 1;if(a->kind==AX_CALL&&!constructors_known(a->text)){LF_Span s={file,0,0,1,1,1,1};seterr(err,s,"unknown canonical AST constructor '%s'",a->text);return 0;}for(size_t i=0;i<a->nargs;i++)if(!validate_action(a->args[i],err,file))return 0;return 1;}
static int validate_refs_expr(LF_Profile*p,GExpr*g,LF_Error*err,const char*file){if(!g)return 1;if(g->kind==GX_REF&&!find_rule(p,g->text)){LF_Span s={file,0,0,1,1,1,1};seterr(err,s,"undefined grammar rule '%s'",g->text);return 0;}for(size_t i=0;i<g->nitems;i++)if(!validate_refs_expr(p,g->items[i],err,file))return 0;return 1;}

static int grammar_has_capture(GExpr *g, const char *name) {
    if(!g) return 0;
    if(g->kind==GX_CAPTURE && strcmp(g->text,name)==0) return 1;
    for(size_t i=0;i<g->nitems;i++) if(grammar_has_capture(g->items[i],name)) return 1;
    return 0;
}

static int validate_action_refs(GExpr *g, AExpr *a, LF_Error *err, const char *file) {
    if(!a) return 1;
    if(a->kind==AX_REF && !grammar_has_capture(g,a->text)) {
        LF_Span s={file,0,0,1,1,1,1};
        seterr(err,s,"AST mapping references unknown capture '%s'",a->text);
        return 0;
    }
    for(size_t i=0;i<a->nargs;i++) if(!validate_action_refs(g,a->args[i],err,file)) return 0;
    return 1;
}

static int gnullable(LF_Profile *p, GExpr *g, unsigned depth) {
    if(!g || depth>p->nrules+8) return 0;
    switch(g->kind) {
    case GX_OPT: case GX_REP: return 1;
    case GX_CAPTURE: return gnullable(p,g->items[0],depth+1);
    case GX_LIT: case GX_CLASS: return 0;
    case GX_REF: { Rule *r=find_rule(p,g->text); return r ? gnullable(p,r->expr,depth+1) : 0; }
    case GX_SEQ:
        for(size_t i=0;i<g->nitems;i++) if(!gnullable(p,g->items[i],depth+1)) return 0;
        return 1;
    case GX_ALT:
        for(size_t i=0;i<g->nitems;i++) if(gnullable(p,g->items[i],depth+1)) return 1;
        return 0;
    }
    return 0;
}

static int validate_nonempty_repetitions(LF_Profile *p, GExpr *g, LF_Error *err, const char *file) {
    if(!g) return 1;
    if(g->kind==GX_REP && gnullable(p,g->items[0],0)) {
        LF_Span s={file,0,0,1,1,1,1};
        seterr(err,s,"repetition body can match empty input");
        return 0;
    }
    for(size_t i=0;i<g->nitems;i++) if(!validate_nonempty_repetitions(p,g->items[i],err,file)) return 0;
    return 1;
}

typedef struct { size_t *v; size_t n,cap; } IdxVec;
static void idxadd(IdxVec *x,size_t v){for(size_t i=0;i<x->n;i++)if(x->v[i]==v)return;if(x->n==x->cap){x->cap=x->cap?x->cap*2:8;x->v=(size_t*)xrealloc(x->v,x->cap*sizeof(*x->v));}x->v[x->n++]=v;}
static size_t rule_index(LF_Profile*p,const char*n){for(size_t i=0;i<p->nrules;i++)if(strcmp(p->rules[i].name,n)==0)return i;return (size_t)-1;}
static void first_refs(LF_Profile*p,GExpr*g,IdxVec*out){
    if(!g)return;
    switch(g->kind){
    case GX_REF:{size_t i=rule_index(p,g->text);if(i!=(size_t)-1)idxadd(out,i);break;}
    case GX_CAPTURE:first_refs(p,g->items[0],out);break;
    case GX_OPT:case GX_REP:first_refs(p,g->items[0],out);break;
    case GX_ALT:for(size_t i=0;i<g->nitems;i++)first_refs(p,g->items[i],out);break;
    case GX_SEQ:
        for(size_t i=0;i<g->nitems;i++){first_refs(p,g->items[i],out);if(!gnullable(p,g->items[i],0))break;}
        break;
    case GX_LIT:case GX_CLASS:break;
    }
}
static int leftdfs(LF_Profile*p,IdxVec*edges,size_t u,unsigned char*state,LF_Error*err,const char*file){
    state[u]=1;
    for(size_t j=0;j<edges[u].n;j++){size_t v=edges[u].v[j];if(state[v]==1){LF_Span s={file,0,0,1,1,1,1};seterr(err,s,"left recursion involving rule '%s'",p->rules[v].name);return 0;}if(state[v]==0&&!leftdfs(p,edges,v,state,err,file))return 0;}
    state[u]=2;return 1;
}
static int validate_left_recursion(LF_Profile*p,LF_Error*err,const char*file){
    IdxVec*e=(IdxVec*)calloc(p->nrules,sizeof(*e));unsigned char*st=(unsigned char*)calloc(p->nrules,1);if(!e||!st)abort();
    for(size_t i=0;i<p->nrules;i++)first_refs(p,p->rules[i].expr,&e[i]);
    int ok=1;for(size_t i=0;i<p->nrules&&ok;i++)if(st[i]==0)ok=leftdfs(p,e,i,st,err,file);
    for(size_t i=0;i<p->nrules;i++) free(e[i].v);
    free(e);
    free(st);
    return ok;
}

/* Validate one comment opener spelling against the fixed lexical classes:
** STRING quotes, long-string '[', NAME start and NUMBER start.  A comment
** scanner runs before token scanning, so such an opener would shadow the
** fixed class and must be rejected. */
static int comment_shadows_fixed_class(const char *s, const char **why) {
    unsigned char c=(unsigned char)s[0];
    if(c=='"'||c=='\''){*why="STRING";return 1;}
    if(c=='['){*why="long string";return 1;}
    if(isalpha(c)||c=='_'){*why="NAME";return 1;}
    if(isdigit(c)||(c=='.'&&isdigit((unsigned char)s[1]))){*why="NUMBER";return 1;}
    return 0;
}

static int is_str_prefix_of(const char *p, size_t np, const char *q) {
    size_t nq=strlen(q);
    return np<nq && memcmp(p,q,np)==0;
}

/* Validate the declared comment set.  Rules (asymmetric on purpose):
** - delimiters must be non-empty and single-line;
** - an opener must not shadow a fixed lexical class (STRING/NAME/NUMBER/...);
** - an opener must not equal or be a prefix of a grammar terminal (the
**   comment scanner runs first and would make the terminal unreachable);
**   the reverse — a terminal that is a prefix of an opener — is fine;
** - the same opener in two comment categories is ambiguous (exception:
**   line_comment + lua_long_comment with the same marker, because the long
**   candidate is strictly longer whenever a long bracket follows). */
static int validate_comments(LF_Profile *p, LF_Error *err, const char *file) {
    if(!p->comments_explicit) return 1;
    for(size_t i=0;i<p->nline_comments;i++){
        const char*s=p->line_comments[i]; size_t ns=strlen(s); const char*why;
        LF_Span sp={file,0,0,1,1,1,1};
        if(ns==0||strchr(s,'\n')||strchr(s,'\r')){seterr(err,sp,"line_comment prefix must be non-empty and single-line");return 0;}
        if(comment_shadows_fixed_class(s,&why)){seterr(err,sp,"line_comment \"%s\" shadows fixed lexical class %s",s,why);return 0;}
        for(size_t j=0;j<p->nliterals;j++){
            if(ns==strlen(p->literals[j])&&strcmp(s,p->literals[j])==0){seterr(err,sp,"lexical spelling '%s' is both a comment and a grammar terminal",s);return 0;}
            if(is_str_prefix_of(s,ns,p->literals[j])){seterr(err,sp,"comment opener '%s' shadows longer grammar terminal '%s'",s,p->literals[j]);return 0;}
        }
        for(size_t j=0;j<p->nblock_comments;j++)
            if(strcmp(s,p->block_comments[j].open)==0){seterr(err,sp,"ambiguous comment opener '%s' declared as both line and block comment",s);return 0;}
    }
    for(size_t i=0;i<p->nblock_comments;i++){
        const char*o=p->block_comments[i].open; size_t no=strlen(o);
        const char*c=p->block_comments[i].close; size_t nc=strlen(c);
        const char*why;
        LF_Span sp={file,0,0,1,1,1,1};
        if(no==0||nc==0||strchr(o,'\n')||strchr(o,'\r')||strchr(c,'\n')||strchr(c,'\r')){seterr(err,sp,"block_comment delimiters must be non-empty and single-line");return 0;}
        if(comment_shadows_fixed_class(o,&why)){seterr(err,sp,"block_comment opener \"%s\" shadows fixed lexical class %s",o,why);return 0;}
        for(size_t j=0;j<p->nliterals;j++){
            if(no==strlen(p->literals[j])&&strcmp(o,p->literals[j])==0){seterr(err,sp,"lexical spelling '%s' is both a comment and a grammar terminal",o);return 0;}
            if(is_str_prefix_of(o,no,p->literals[j])){seterr(err,sp,"comment opener '%s' shadows longer grammar terminal '%s'",o,p->literals[j]);return 0;}
        }
        for(size_t j=0;j<p->nblock_comments;j++)
            if(i!=j&&strcmp(o,p->block_comments[j].open)==0){seterr(err,sp,"duplicate block_comment opener '%s'",o);return 0;}
        for(size_t j=0;j<p->nlong_markers;j++){
            size_t nm=strlen(p->long_markers[j]);
            if(nm<=no&&memcmp(p->long_markers[j],o,nm)==0){seterr(err,sp,"ambiguous comment opener '%s': lua_long_comment marker overlaps block_comment opener '%s'",p->long_markers[j],o);return 0;}
        }
    }
    for(size_t i=0;i<p->nlong_markers;i++){
        const char*s=p->long_markers[i]; size_t ns=strlen(s); const char*why;
        LF_Span sp={file,0,0,1,1,1,1};
        if(ns==0||strchr(s,'\n')||strchr(s,'\r')){seterr(err,sp,"lua_long_comment marker must be non-empty and single-line");return 0;}
        if(comment_shadows_fixed_class(s,&why)){seterr(err,sp,"lua_long_comment marker \"%s\" shadows fixed lexical class %s",s,why);return 0;}
        for(size_t j=0;j<p->nliterals;j++){
            if(ns==strlen(p->literals[j])&&strcmp(s,p->literals[j])==0){seterr(err,sp,"lexical spelling '%s' is both a comment and a grammar terminal",s);return 0;}
            if(is_str_prefix_of(s,ns,p->literals[j])){seterr(err,sp,"comment opener '%s' shadows longer grammar terminal '%s'",s,p->literals[j]);return 0;}
        }
    }
    return 1;
}

static int validate_profile(LF_Profile *p, LF_Error *err, const char *file) {
    if(!find_rule(p,"chunk")){
        LF_Span sp={file,0,0,1,1,1,1};
        seterr(err,sp,"profile has no required root rule 'chunk'");
        return 0;
    }
    for(size_t i=0;i<p->nrules;i++){
        if(!validate_refs_expr(p,p->rules[i].expr,err,file) ||
           !validate_action(p->rules[i].action,err,file) ||
           !validate_action_refs(p->rules[i].expr,p->rules[i].action,err,file) ||
           !validate_nonempty_repetitions(p,p->rules[i].expr,err,file)) return 0;
    }
    if(!validate_comments(p,err,file)) return 0;
    return validate_left_recursion(p,err,file);
}

LF_Profile *lf_profile_load_memory(const char *source_name, const char *source,
                                   size_t source_len, LF_Error *err){
    if (err) memset(err, 0, sizeof(*err));
    const char *path=source_name?source_name:"=(syntax profile)";
    char *buf=(char*)xmalloc(source_len+1);memcpy(buf,source,source_len);buf[source_len]=0;
    PScan q={buf,source_len,0,1,1,path,{0},err,0};pnext(&q);
    LF_Profile*p=(LF_Profile*)calloc(1,sizeof(*p));if(!p)abort();
    if(q.cur.k!=PT_ID||strcmp(q.cur.text,"profile")!=0){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"profile must start with 'profile <name>;' ");goto bad;}pnext(&q);
    if(q.cur.k!=PT_ID){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"expected profile name");goto bad;}p->name=xstrdup(q.cur.text);pnext(&q);if(!pexpect(&q,PT_SEMI,"';'"))goto bad;
    while(q.cur.k!=PT_EOF&&!q.failed){
        if(q.cur.k==PT_ID && strcmp(q.cur.text,"version")==0){
            pnext(&q);
            if(p->version!=NULL){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"duplicate profile version");goto bad;}
            if(q.cur.k!=PT_STR){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"expected string after 'version'");goto bad;}
            p->version=xstrdup(q.cur.text); pnext(&q);
            if(!pexpect(&q,PT_SEMI,"';'"))goto bad;
            continue;
        }
        if(q.cur.k==PT_ID && strcmp(q.cur.text,"contextual")==0){
            pnext(&q);
            if(q.cur.k!=PT_STR){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"expected string after 'contextual'");goto bad;}
            add_contextual(p,q.cur.text); pnext(&q);
            if(!pexpect(&q,PT_SEMI,"';'"))goto bad;
            continue;
        }
        if(q.cur.k==PT_ID && strcmp(q.cur.text,"line_comment")==0){
            pnext(&q);
            if(q.cur.k!=PT_STR){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"expected string after 'line_comment'");goto bad;}
            add_line_comment(p,q.cur.text); p->comments_explicit=1; pnext(&q);
            if(!pexpect(&q,PT_SEMI,"';'"))goto bad;
            continue;
        }
        if(q.cur.k==PT_ID && strcmp(q.cur.text,"block_comment")==0){
            pnext(&q);
            if(q.cur.k!=PT_STR){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"expected opener string after 'block_comment'");goto bad;}
            char *bo=xstrdup(q.cur.text); pnext(&q);
            if(q.cur.k!=PT_STR){free(bo);seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"expected closer string after 'block_comment' opener");goto bad;}
            add_block_comment(p,bo,q.cur.text); p->comments_explicit=1; free(bo); pnext(&q);
            if(!pexpect(&q,PT_SEMI,"';'"))goto bad;
            continue;
        }
        if(q.cur.k==PT_ID && strcmp(q.cur.text,"lua_long_comment")==0){
            pnext(&q);
            if(q.cur.k!=PT_STR){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"expected string after 'lua_long_comment'");goto bad;}
            add_long_marker(p,q.cur.text); p->comments_explicit=1; pnext(&q);
            if(!pexpect(&q,PT_SEMI,"';'"))goto bad;
            continue;
        }
        if(q.cur.k!=PT_ID||strcmp(q.cur.text,"rule")!=0){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"expected 'rule', 'version', 'contextual', 'line_comment', 'block_comment', or 'lua_long_comment'");goto bad;}pnext(&q);
        if(q.cur.k!=PT_ID){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"expected rule name");goto bad;}char *rn=xstrdup(q.cur.text);pnext(&q);
        if(find_rule(p,rn)){seterr(err,pspan(&q,q.cur.off,q.cur.line,q.cur.col),"duplicate rule '%s'",rn);free(rn);goto bad;}
        if(!pexpect(&q,PT_EQ,"'='")){free(rn);goto bad;}GExpr*g=parse_galt(&q);if(!g||q.failed){free(rn);gfree(g);goto bad;}AExpr*a=NULL;if(paccept(&q,PT_ARROW)){a=parse_action(&q);if(!a){free(rn);gfree(g);goto bad;}}if(!pexpect(&q,PT_SEMI,"';'")){free(rn);gfree(g);afree(a);goto bad;}
        if(p->nrules==p->caprules){p->caprules=p->caprules?p->caprules*2:32;p->rules=(Rule*)xrealloc(p->rules,p->caprules*sizeof(*p->rules));}p->rules[p->nrules++]=(Rule){rn,g,a};collect_literals(p,g);
    }
    free(q.cur.text);free(buf);
    if(!validate_profile(p,err,path)) goto bad2;
    return p;
bad: free(q.cur.text);free(buf);
bad2: if(err && err->kind!=LF_ERROR_IO) err->kind=LF_ERROR_PROFILE; lf_profile_free(p);return NULL;
}


/* ---------- profile loading and discovery ----------
**
** External profiles are parsed on every use.  Realistic profiles are a few
** kilobytes and parse in well under a millisecond, so no compiled-profile
** cache is kept on disk.
*/

static char *path_join2(const char*a,const char*b){size_t na=strlen(a),nb=strlen(b);int slash=(na>0&&a[na-1]!='/');char*r=(char*)xmalloc(na+(size_t)slash+nb+1);memcpy(r,a,na);if(slash)r[na++]='/';memcpy(r+na,b,nb+1);return r;}
LF_Profile *lf_profile_load(const char *path, LF_Error *err){
    if (err) memset(err, 0, sizeof(*err));
    FILE*f=fopen(path,"rb");if(!f){LF_Span sp={path,0,0,1,1,1,1};seterrk(err,LF_ERROR_IO,sp,"cannot open profile: %s",strerror(errno));return NULL;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}long z=ftell(f);if(z<0){fclose(f);return NULL;}rewind(f);
    char *buf=(char*)xmalloc((size_t)z+1);if(fread(buf,1,(size_t)z,f)!=(size_t)z){fclose(f);free(buf);return NULL;}fclose(f);buf[z]=0;
    LF_Profile*p=lf_profile_load_memory(path,buf,(size_t)z,err);
    free(buf);
    if (!p && err && err->kind != LF_ERROR_IO) err->kind = LF_ERROR_PROFILE;
    return p;
}

static int profile_path_exists(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0;
}

LF_Profile *lf_profile_discover(const char *explicit_path, int noenv,
                                LF_Error *err) {
    if (err) memset(err, 0, sizeof(*err));
    if (explicit_path != NULL)
        return lf_profile_load(explicit_path, err);

    if (!noenv) {
        const char *env = getenv("LUA_SYNTAX");
        if (env != NULL && *env != '\0')
            return lf_profile_load(env, err);
    }

    if (profile_path_exists(".lua-syntax"))
        return lf_profile_load(".lua-syntax", err);

    if (!noenv) {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg != NULL && *xdg != '\0') {
            char *lua = path_join2(xdg, "lua");
            char *path = path_join2(lua, "syntax");
            free(lua);
            if (profile_path_exists(path)) {
                LF_Profile *p = lf_profile_load(path, err);
                free(path);
                return p;
            }
            free(path);
        }
        const char *home = getenv("HOME");
        if (home != NULL && *home != '\0') {
            char *cfg = path_join2(home, ".config");
            char *lua = path_join2(cfg, "lua");
            char *path = path_join2(lua, "syntax");
            free(cfg); free(lua);
            if (profile_path_exists(path)) {
                LF_Profile *p = lf_profile_load(path, err);
                free(path);
                return p;
            }
            free(path);
        }
    }

    if (profile_path_exists("/etc/lua/syntax"))
        return lf_profile_load("/etc/lua/syntax", err);

    return lf_profile_builtin_lua55(err);
}

void lf_profile_free(LF_Profile*p){if(!p)return;free(p->name);free(p->version);for(size_t i=0;i<p->nrules;i++){free(p->rules[i].name);gfree(p->rules[i].expr);afree(p->rules[i].action);}free(p->rules);for(size_t i=0;i<p->nliterals;i++)free(p->literals[i]);free(p->literals);for(size_t i=0;i<p->ncontextuals;i++)free(p->contextuals[i]);free(p->contextuals);for(size_t i=0;i<p->nline_comments;i++)free(p->line_comments[i]);free(p->line_comments);for(size_t i=0;i<p->nblock_comments;i++){free(p->block_comments[i].open);free(p->block_comments[i].close);}free(p->block_comments);for(size_t i=0;i<p->nlong_markers;i++)free(p->long_markers[i]);free(p->long_markers);free(p);}
const char*lf_profile_name(const LF_Profile*p){return p?p->name:NULL;}
const char*lf_profile_version(const LF_Profile*p){return p?p->version:NULL;}
size_t lf_profile_rule_count(const LF_Profile*p){return p?p->nrules:0;}

/* ---------- source lexer ---------- */

typedef enum { ST_EOF, ST_NAME, ST_NUMBER, ST_STRING, ST_LITERAL } SKind;
typedef struct { SKind k; char *text; size_t len; LF_Span span; } STok;
typedef struct { STok *v; size_t n,cap; } Tokens;
static void tokpush(Tokens*t,STok x){if(t->n==t->cap){t->cap=t->cap?t->cap*2:128;t->v=(STok*)xrealloc(t->v,t->cap*sizeof(*t->v));}t->v[t->n++]=x;}
static void toksfree(Tokens*t){for(size_t i=0;i<t->n;i++)free(t->v[i].text);free(t->v);}

static int iswordlit(const char*s){return s[0]&&(isalpha((unsigned char)s[0])||s[0]=='_');}
static size_t scan_numeral(const char *s, size_t n, size_t i) {
    size_t p=i;
    if(p+2<=n && s[p]=='0' && p+1<n && (s[p+1]=='x'||s[p+1]=='X')) {
        p+=2;
        while(p<n && isxdigit((unsigned char)s[p])) p++;
        if(p<n && s[p]=='.' && !(p+1<n && s[p+1]=='.')) {
            p++;
            while(p<n && isxdigit((unsigned char)s[p])) p++;
        }
        if(p<n && (s[p]=='p'||s[p]=='P')) {
            p++;
            if(p<n && (s[p]=='+'||s[p]=='-')) p++;
            while(p<n && isdigit((unsigned char)s[p])) p++;
        }
    } else {
        if(p<n && s[p]=='.') {
            p++;
            while(p<n && isdigit((unsigned char)s[p])) p++;
        } else {
            while(p<n && isdigit((unsigned char)s[p])) p++;
            if(p<n && s[p]=='.' && !(p+1<n && s[p+1]=='.')) {
                p++;
                while(p<n && isdigit((unsigned char)s[p])) p++;
            }
        }
        if(p<n && (s[p]=='e'||s[p]=='E')) {
            p++;
            if(p<n && (s[p]=='+'||s[p]=='-')) p++;
            while(p<n && isdigit((unsigned char)s[p])) p++;
        }
    }
    return p;
}

static const char *match_literal(LF_Profile*p,const char*s,size_t n,size_t *mlen){const char*best=NULL;*mlen=0;for(size_t i=0;i<p->nliterals;i++){const char*l=p->literals[i];size_t z=strlen(l);if(z<=*mlen||z>n)continue;if(memcmp(s,l,z)!=0)continue;if(iswordlit(l)){if(z<n&&(isalnum((unsigned char)s[z])||s[z]=='_'))continue;}best=l;*mlen=z;}return best;}
static int long_bracket(const char*s,size_t n,size_t pos,size_t *eqs,size_t *openlen){if(pos>=n||s[pos]!='[')return 0;size_t i=pos+1,e=0;while(i<n&&s[i]=='='){e++;i++;}if(i<n&&s[i]=='['){*eqs=e;*openlen=i-pos+1;return 1;}return 0;}
static int ascii_space(unsigned char c) {
    return c==' ' || c=='\f' || c=='\n' || c=='\r' || c=='\t' || c=='\v';
}
static int ascii_alpha(unsigned char c) {
    return (c>='A'&&c<='Z') || (c>='a'&&c<='z') || c=='_';
}
static int ascii_alnum(unsigned char c) {
    return ascii_alpha(c) || (c>='0'&&c<='9');
}
static int hexval(unsigned char c) {
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
}

typedef struct { char *v; size_t n,cap; } ByteBuf;
static void bbpush(ByteBuf*b,unsigned char c){if(b->n+1>=b->cap){b->cap=b->cap?b->cap*2:32;b->v=(char*)xrealloc(b->v,b->cap);}b->v[b->n++]=(char)c;}
static char *bbfinish(ByteBuf*b){if(b->n+1>=b->cap){b->cap=b->n+1;b->v=(char*)xrealloc(b->v,b->cap);}b->v[b->n]=0;return b->v;}

static void consume_newline(const char*s,size_t n,size_t*i,unsigned*line,unsigned*col){
    unsigned char first=(unsigned char)s[*i];
    (*i)++;
    if(*i<n){unsigned char second=(unsigned char)s[*i];if((first=='\n'&&second=='\r')||(first=='\r'&&second=='\n'))(*i)++;}
    (*line)++;*col=1;
}

static int utf8_append(ByteBuf*b,uint32_t x){
    unsigned char out[6];int n;
    if(x<0x80){out[0]=(unsigned char)x;n=1;}
    else if(x<0x800){out[0]=(unsigned char)(0xC0|(x>>6));out[1]=(unsigned char)(0x80|(x&0x3F));n=2;}
    else if(x<0x10000){out[0]=(unsigned char)(0xE0|(x>>12));out[1]=(unsigned char)(0x80|((x>>6)&0x3F));out[2]=(unsigned char)(0x80|(x&0x3F));n=3;}
    else if(x<0x200000){out[0]=(unsigned char)(0xF0|(x>>18));out[1]=(unsigned char)(0x80|((x>>12)&0x3F));out[2]=(unsigned char)(0x80|((x>>6)&0x3F));out[3]=(unsigned char)(0x80|(x&0x3F));n=4;}
    else if(x<0x4000000){out[0]=(unsigned char)(0xF8|(x>>24));out[1]=(unsigned char)(0x80|((x>>18)&0x3F));out[2]=(unsigned char)(0x80|((x>>12)&0x3F));out[3]=(unsigned char)(0x80|((x>>6)&0x3F));out[4]=(unsigned char)(0x80|(x&0x3F));n=5;}
    else if(x<=0x7FFFFFFFu){out[0]=(unsigned char)(0xFC|(x>>30));out[1]=(unsigned char)(0x80|((x>>24)&0x3F));out[2]=(unsigned char)(0x80|((x>>18)&0x3F));out[3]=(unsigned char)(0x80|((x>>12)&0x3F));out[4]=(unsigned char)(0x80|((x>>6)&0x3F));out[5]=(unsigned char)(0x80|(x&0x3F));n=6;}
    else return 0;
    for(int i=0;i<n;i++) bbpush(b,out[i]);
    return 1;
}

static int lex_short_string(const char*file,const char*s,size_t n,size_t*i,
                            unsigned*line,unsigned*col,STok*out,LF_Error*err){
    size_t st=*i;unsigned sl=*line,sc=*col;unsigned char quote=(unsigned char)s[(*i)++];(*col)++;
    ByteBuf b={0};
    while(*i<n){
        unsigned char c=(unsigned char)s[*i];
        if(c==quote){(*i)++;(*col)++;char*data=bbfinish(&b);LF_Span sp={file,st,*i,sl,sc,*line,*col};*out=(STok){ST_STRING,data,b.n,sp};return 1;}
        if(c=='\n'||c=='\r'){free(b.v);LF_Span sp={file,st,*i,sl,sc,*line,*col};seterrk(err,LF_ERROR_INCOMPLETE,sp,"unfinished string");return 0;}
        if(c!='\\'){bbpush(&b,c);(*i)++;(*col)++;continue;}
        (*i)++;(*col)++;
        if(*i>=n){free(b.v);LF_Span sp={file,st,*i,sl,sc,*line,*col};seterrk(err,LF_ERROR_INCOMPLETE,sp,"unfinished string");return 0;}
        c=(unsigned char)s[*i];
        switch(c){
        case 'a':bbpush(&b,'\a');(*i)++;(*col)++;break;
        case 'b':bbpush(&b,'\b');(*i)++;(*col)++;break;
        case 'f':bbpush(&b,'\f');(*i)++;(*col)++;break;
        case 'n':bbpush(&b,'\n');(*i)++;(*col)++;break;
        case 'r':bbpush(&b,'\r');(*i)++;(*col)++;break;
        case 't':bbpush(&b,'\t');(*i)++;(*col)++;break;
        case 'v':bbpush(&b,'\v');(*i)++;(*col)++;break;
        case '\\':case '"':case '\'':bbpush(&b,c);(*i)++;(*col)++;break;
        case '\n':case '\r':consume_newline(s,n,i,line,col);bbpush(&b,'\n');break;
        case 'z':
            (*i)++;(*col)++;
            while(*i<n&&ascii_space((unsigned char)s[*i])){if(s[*i]=='\n'||s[*i]=='\r')consume_newline(s,n,i,line,col);else{(*i)++;(*col)++;}}
            break;
        case 'x': {
            (*i)++;(*col)++;
            if(*i+2>n||hexval((unsigned char)s[*i])<0||hexval((unsigned char)s[*i+1])<0){free(b.v);LF_Span sp={file,*i,*i,sl,sc,*line,*col};seterr(err,sp,"hexadecimal digit expected");return 0;}
            int v=(hexval((unsigned char)s[*i])<<4)|hexval((unsigned char)s[*i+1]);bbpush(&b,(unsigned char)v);*i+=2;*col+=2;break;
        }
        case 'u': {
            (*i)++;(*col)++;
            if(*i>=n||s[*i]!='{'){free(b.v);LF_Span sp={file,*i,*i,sl,sc,*line,*col};seterr(err,sp,"missing '{' in UTF-8 escape");return 0;}
            (*i)++;(*col)++;uint32_t v=0;int digits=0;
            while(*i<n&&hexval((unsigned char)s[*i])>=0){int h=hexval((unsigned char)s[*i]);if(v>(0x7FFFFFFFu>>4)){free(b.v);LF_Span sp={file,*i,*i,sl,sc,*line,*col};seterr(err,sp,"UTF-8 value too large");return 0;}v=(v<<4)|(uint32_t)h;digits++;(*i)++;(*col)++;}
            if(!digits||*i>=n||s[*i]!='}'){free(b.v);LF_Span sp={file,*i,*i,sl,sc,*line,*col};seterr(err,sp,digits?"missing '}' in UTF-8 escape":"hexadecimal digit expected");return 0;}
            (*i)++;(*col)++;if(!utf8_append(&b,v)){free(b.v);LF_Span sp={file,*i,*i,sl,sc,*line,*col};seterr(err,sp,"UTF-8 value too large");return 0;}break;
        }
        default:
            if(c>='0'&&c<='9'){
                unsigned v=0;int d=0;while(d<3&&*i<n&&s[*i]>='0'&&s[*i]<='9'){v=v*10+(unsigned)(s[*i]-'0');(*i)++;(*col)++;d++;}
                if(v>255){free(b.v);LF_Span sp={file,*i,*i,sl,sc,*line,*col};seterr(err,sp,"decimal escape too large");return 0;}bbpush(&b,(unsigned char)v);
            }else{free(b.v);LF_Span sp={file,*i,*i+1,sl,sc,*line,*col};seterr(err,sp,"invalid escape sequence");return 0;}
        }
    }
    free(b.v);LF_Span sp={file,st,*i,sl,sc,*line,*col};seterrk(err,LF_ERROR_INCOMPLETE,sp,"unfinished string");return 0;
}

static int match_long_close(const char*s,size_t n,size_t pos,size_t eq,size_t*close_len){
    size_t j,e=0;
    if(pos>=n||s[pos]!=']') return 0;
    j=pos+1;
    while(j<n&&s[j]=='='){e++;j++;}
    if(e==eq&&j<n&&s[j]==']'){*close_len=j-pos+1;return 1;}
    return 0;
}

static int scan_long(const char*file,const char*s,size_t n,size_t*i,unsigned*line,unsigned*col,
                     size_t eq,size_t openlen,int want_value,STok*out,LF_Error*err,size_t st,unsigned sl,unsigned sc){
    *i+=openlen;*col+=(unsigned)openlen;
    if(*i<n&&(s[*i]=='\n'||s[*i]=='\r'))consume_newline(s,n,i,line,col);
    ByteBuf b={0};
    for(;;){
        if(*i>=n){free(b.v);LF_Span sp={file,st,*i,sl,sc,*line,*col};seterrk(err,LF_ERROR_INCOMPLETE,sp,want_value?"unfinished long string":"unfinished long comment");return 0;}
        size_t close_len=0;if(match_long_close(s,n,*i,eq,&close_len)){*i+=close_len;*col+=(unsigned)close_len;if(want_value){char*data=bbfinish(&b);LF_Span sp={file,st,*i,sl,sc,*line,*col};*out=(STok){ST_STRING,data,b.n,sp};}else free(b.v);return 1;}
        if(s[*i]=='\n'||s[*i]=='\r'){consume_newline(s,n,i,line,col);if(want_value)bbpush(&b,'\n');}
        else{if(want_value)bbpush(&b,(unsigned char)s[*i]);(*i)++;(*col)++;}
    }
}

/* ---- configurable comment trivia ----
**
** Comments are pure lexer-level trivia: they are skipped between any two
** meaningful tokens and never reach the grammar or the canonical AST.  On
** each position the comment scanner runs after whitespace and before any
** token scanning; among all candidates (lua_long marker+bracket, block
** opener, line prefix) the longest match wins.  Validation rejects profiles
** where that order could still be ambiguous.  A profile without comment
** directives keeps the built-in Lua set: line '--' plus long '--' + bracket.
*/

enum { LF_COMMENT_NONE=0, LF_COMMENT_LINE, LF_COMMENT_BLOCK, LF_COMMENT_LUA_LONG };

typedef struct {
    int kind;
    size_t len;         /* full candidate length used for longest match */
    size_t marker_len;  /* lua_long_comment: marker length; pos+len is at the bracket */
    size_t eq, openlen; /* lua_long_comment bracket level / opener length */
    size_t block;       /* block comment index */
} CommentMatch;

static int match_comment(const LF_Profile *p, const char *s, size_t n, size_t i, CommentMatch *m) {
    size_t best=0; int kind=LF_COMMENT_NONE;
    size_t bmark=0,beq=0,bopen=0,bidx=0;
    size_t nlong=p->comments_explicit?p->nlong_markers:1;
    for(size_t k=0;k<nlong;k++){
        const char*mark=p->comments_explicit?p->long_markers[k]:"--";
        size_t l=strlen(mark);
        if(n-i<l||memcmp(s+i,mark,l)!=0) continue;
        size_t eq=0,ol=0;
        if(!long_bracket(s,n,i+l,&eq,&ol)) continue;
        if(l+ol>best){best=l+ol;kind=LF_COMMENT_LUA_LONG;bmark=l;beq=eq;bopen=ol;}
    }
    size_t nblock=p->comments_explicit?p->nblock_comments:0;
    for(size_t k=0;k<nblock;k++){
        size_t l=strlen(p->block_comments[k].open);
        if(n-i<l||memcmp(s+i,p->block_comments[k].open,l)!=0) continue;
        if(l>best){best=l;kind=LF_COMMENT_BLOCK;bidx=k;}
    }
    size_t nline=p->comments_explicit?p->nline_comments:1;
    for(size_t k=0;k<nline;k++){
        const char*pre=p->comments_explicit?p->line_comments[k]:"--";
        size_t l=strlen(pre);
        if(n-i<l||memcmp(s+i,pre,l)!=0) continue;
        if(l>best){best=l;kind=LF_COMMENT_LINE;}
    }
    if(kind==LF_COMMENT_NONE) return 0;
    m->kind=kind;m->len=best;m->marker_len=bmark;m->eq=beq;m->openlen=bopen;m->block=bidx;
    return 1;
}

static int lex_source(LF_Profile*p,const char*file,const char*s,size_t n,Tokens*out,LF_Error*err){
    size_t i=0;unsigned line=1,col=1;
    while(i<n){unsigned char c=(unsigned char)s[i];
        if(ascii_space(c)){if(c=='\n'||c=='\r')consume_newline(s,n,&i,&line,&col);else{i++;col++;}continue;}
        CommentMatch cm;
        if(match_comment(p,s,n,i,&cm)){
            size_t st=i;unsigned sl=line,sc=col;
            if(cm.kind==LF_COMMENT_LUA_LONG){
                i+=cm.marker_len;col+=(unsigned)cm.marker_len;  /* i now at the bracket */
                STok dummy={0};
                if(!scan_long(file,s,n,&i,&line,&col,cm.eq,cm.openlen,0,&dummy,err,st,sl,sc))return 0;
            }
            else if(cm.kind==LF_COMMENT_BLOCK){
                const char*close=p->block_comments[cm.block].close;
                size_t cl=strlen(close);
                i+=cm.len;col+=(unsigned)cm.len;  /* consume opener */
                for(;;){  /* non-nested: first closing delimiter wins */
                    if(i>=n){LF_Span sp={file,st,i,sl,sc,line,col};seterrk(err,LF_ERROR_INCOMPLETE,sp,"unfinished block comment");return 0;}
                    if(cl<=n-i&&memcmp(s+i,close,cl)==0){i+=cl;col+=(unsigned)cl;break;}
                    if(s[i]=='\n'||s[i]=='\r')consume_newline(s,n,&i,&line,&col);
                    else{i++;col++;}
                }
            }
            else{  /* line comment: skip to end of line, newline left to the main loop */
                i+=cm.len;col+=(unsigned)cm.len;
                while(i<n&&s[i]!='\n'&&s[i]!='\r'){i++;col++;}
            }
            continue;
        }
        size_t st=i;unsigned sl=line,sc=col;
        if(c=='\''||c=='"'){STok tok={0};if(!lex_short_string(file,s,n,&i,&line,&col,&tok,err))return 0;tokpush(out,tok);continue;}
        size_t eq=0,ol=0;if(long_bracket(s,n,i,&eq,&ol)){STok tok={0};if(!scan_long(file,s,n,&i,&line,&col,eq,ol,1,&tok,err,st,sl,sc))return 0;tokpush(out,tok);continue;}
        if(ascii_alpha(c)){i++;col++;while(i<n&&ascii_alnum((unsigned char)s[i])){i++;col++;}size_t z=i-st;char*w=xstrndup(s+st,z);size_t ml=0;const char*l=match_literal(p,w,z,&ml);LF_Span sp={file,st,i,sl,sc,line,col};if(l&&ml==z&&!is_contextual(p,l)){free(w);tokpush(out,(STok){ST_LITERAL,xstrdup(l),strlen(l),sp});}else tokpush(out,(STok){ST_NAME,w,z,sp});continue;}
        if((c>='0'&&c<='9')||(c=='.'&&i+1<n&&s[i+1]>='0'&&s[i+1]<='9')){
            i=scan_numeral(s,n,st);col=sc+(unsigned)(i-st);LF_Span sp={file,st,i,sl,sc,line,col};tokpush(out,(STok){ST_NUMBER,xstrndup(s+st,i-st),i-st,sp});continue;
        }
        size_t ml=0;const char*l=match_literal(p,s+i,n-i,&ml);if(l){i+=ml;col+=(unsigned)ml;LF_Span sp={file,st,i,sl,sc,line,col};tokpush(out,(STok){ST_LITERAL,xstrdup(l),strlen(l),sp});continue;}
        LF_Span sp={file,st,st+1,sl,sc,sl,sc+1};seterr(err,sp,"unexpected character '%c'",c);return 0;
    }
    LF_Span eof={file,n,n,line,col,line,col};tokpush(out,(STok){ST_EOF,xstrdup("<eof>"),5,eof});return 1;
}

/* ---------- AST and generic parser ---------- */

typedef struct { char *text; size_t len; LF_Span span; } AstLiteralOrigin;
struct LF_Ast {
    char *kind; char *text; size_t text_len; LF_Span span;
    LF_Ast **children; char **fields; size_t n,cap;
    AstLiteralOrigin *origins; size_t norigins, caporigins;
};
static LF_Ast *astnew(const char*k,LF_Span sp){LF_Ast*a=(LF_Ast*)calloc(1,sizeof(*a));if(!a)abort();a->kind=xstrdup(k);a->span=sp;return a;}
static LF_Ast *asttextn(const char*k,const char*t,size_t len,LF_Span sp){LF_Ast*a=astnew(k,sp);a->text=xstrndup(t,len);a->text_len=len;return a;}
static LF_Ast *asttext(const char*k,const char*t,LF_Span sp){return asttextn(k,t,strlen(t),sp);}
static int ast_span_contributes(const LF_Ast *c){
    if(!c)return 0;
    if(strcmp(c->kind,"_Absent")==0)return 0;
    if(strcmp(c->kind,"List")==0 && c->n==0)return 0;
    return 1;
}
static void astadd(LF_Ast*a,const char*field,LF_Ast*c){
    if(!c)return;
    int contributes=ast_span_contributes(c);
    size_t oldn=a->n;
    if(a->n==a->cap){a->cap=a->cap?a->cap*2:8;a->children=(LF_Ast**)xrealloc(a->children,a->cap*sizeof(*a->children));a->fields=(char**)xrealloc(a->fields,a->cap*sizeof(*a->fields));}
    a->children[a->n]=c;a->fields[a->n]=field?xstrdup(field):NULL;a->n++;
    if(contributes){
        int had=0;for(size_t i=0;i<oldn;i++)if(ast_span_contributes(a->children[i])){had=1;break;}
        if(!had)a->span=c->span;
        else {a->span.byte_end=c->span.byte_end;a->span.line_end=c->span.line_end;a->span.col_end=c->span.col_end;}
    }
}
static void astorigin_add(LF_Ast *a,const char *text,size_t len,LF_Span sp){
    if(!a||!text)return;
    if(a->norigins==a->caporigins){a->caporigins=a->caporigins?a->caporigins*2:8;a->origins=(AstLiteralOrigin*)xrealloc(a->origins,a->caporigins*sizeof(*a->origins));}
    AstLiteralOrigin *o=&a->origins[a->norigins++];o->text=xstrndup(text,len);o->len=len;o->span=sp;
}
static void astorigin_copy_all(LF_Ast *dst,const LF_Ast *src){
    if(!dst||!src)return;
    for(size_t i=0;i<src->norigins;i++)astorigin_add(dst,src->origins[i].text,src->origins[i].len,src->origins[i].span);
}
void lf_ast_free(LF_Ast*a){if(!a)return;free(a->kind);free(a->text);for(size_t i=0;i<a->n;i++){free(a->fields[i]);lf_ast_free(a->children[i]);}for(size_t i=0;i<a->norigins;i++)free(a->origins[i].text);free(a->origins);free(a->children);free(a->fields);free(a);}

static LF_Ast *astclone(const LF_Ast*a){if(!a)return NULL;LF_Span sp=a->span;LF_Ast*b=astnew(a->kind,sp);if(a->text){b->text=xstrndup(a->text,a->text_len);b->text_len=a->text_len;}for(size_t i=0;i<a->n;i++)astadd(b,a->fields[i],astclone(a->children[i]));astorigin_copy_all(b,a);b->span=sp;return b;}

typedef struct { char *name; LF_Ast **vals; size_t n,cap; } Capture;
typedef struct { Capture *v; size_t n,cap; } Caps;
static void capfree(Caps*c){for(size_t i=0;i<c->n;i++){free(c->v[i].name);for(size_t j=0;j<c->v[i].n;j++)lf_ast_free(c->v[i].vals[j]);free(c->v[i].vals);}free(c->v);}
static Capture *capfind(Caps*c,const char*n){for(size_t i=0;i<c->n;i++)if(strcmp(c->v[i].name,n)==0)return &c->v[i];return NULL;}
static void capadd(Caps*c,const char*n,LF_Ast*a){Capture*x=capfind(c,n);if(!x){if(c->n==c->cap){c->cap=c->cap?c->cap*2:8;c->v=(Capture*)xrealloc(c->v,c->cap*sizeof(*c->v));}x=&c->v[c->n++];memset(x,0,sizeof(*x));x->name=xstrdup(n);}if(x->n==x->cap){x->cap=x->cap?x->cap*2:4;x->vals=(LF_Ast**)xrealloc(x->vals,x->cap*sizeof(*x->vals));}x->vals[x->n++]=a;}

typedef struct { LF_Profile*p; Tokens*t; size_t pos; LF_Error*err; unsigned depth; size_t farthest; const char *expected; int limit_hit; size_t limit_pos; } Parser;

typedef struct { LF_Ast *node; Caps caps; } Match;
static void matchfree(Match*m){lf_ast_free(m->node);capfree(&m->caps);memset(m,0,sizeof(*m));}
static void mergecaps(Caps*d,Caps*s){for(size_t i=0;i<s->n;i++){for(size_t j=0;j<s->v[i].n;j++)capadd(d,s->v[i].name,s->v[i].vals[j]);free(s->v[i].vals);free(s->v[i].name);}free(s->v);memset(s,0,sizeof(*s));}
static void expectat(Parser*q,const char*what){if(q->pos>=q->farthest){q->farthest=q->pos;q->expected=what;}}

static int parse_rule(Parser*q,Rule*r,Match*out);
static int parse_g(Parser*q,GExpr*g,Match*out){memset(out,0,sizeof(*out));
    if(q->depth++>LF_MAX_RECURSION){q->depth--;q->limit_hit=1;if(q->pos>q->limit_pos)q->limit_pos=q->pos;return 0;}
    int ok=0;
    switch(g->kind){
    case GX_LIT:{STok*t=&q->t->v[q->pos];if((t->k==ST_LITERAL||(t->k==ST_NAME&&is_contextual(q->p,g->text)))&&strcmp(t->text,g->text)==0){out->node=asttext("Token",t->text,t->span);q->pos++;ok=1;}else expectat(q,g->text);break;}
    case GX_CLASS:{STok*t=&q->t->v[q->pos];SKind want=ST_EOF;if(strcmp(g->text,"NAME")==0)want=ST_NAME;else if(strcmp(g->text,"NUMBER")==0)want=ST_NUMBER;else if(strcmp(g->text,"STRING")==0)want=ST_STRING;else want=ST_EOF;if(t->k==want){const char*k=want==ST_NAME?"Name":want==ST_NUMBER?"Number":want==ST_STRING?"String":"EOF";out->node=asttextn(k,t->text,t->len,t->span);q->pos++;ok=1;}else expectat(q,g->text);break;}
    case GX_REF:{Rule*r=find_rule(q->p,g->text);size_t st=q->pos;Match m={0};if(r&&parse_rule(q,r,&m)){*out=m;ok=1;}else q->pos=st;break;}
    case GX_CAPTURE:{size_t st=q->pos;Match m={0};if(parse_g(q,g->items[0],&m)){out->node=m.node;m.node=NULL;mergecaps(&out->caps,&m.caps);capadd(&out->caps,g->text,astclone(out->node));ok=1;}else{q->pos=st;matchfree(&m);}break;}
    case GX_SEQ:{size_t st=q->pos;LF_Ast*list=astnew("Seq",q->t->v[q->pos].span);for(size_t i=0;i<g->nitems;i++){Match m={0};if(!parse_g(q,g->items[i],&m)){q->pos=st;lf_ast_free(list);capfree(&out->caps);memset(out,0,sizeof(*out));ok=0;goto done;}astadd(list,NULL,m.node);m.node=NULL;mergecaps(&out->caps,&m.caps);}out->node=list;ok=1;break;}
    case GX_ALT:{for(size_t i=0;i<g->nitems;i++){size_t st=q->pos;Match m={0};if(parse_g(q,g->items[i],&m)){*out=m;ok=1;break;}q->pos=st;matchfree(&m);}break;}
    case GX_OPT:{size_t st=q->pos;Match m={0};if(parse_g(q,g->items[0],&m)){*out=m;ok=1;}else{q->pos=st;matchfree(&m);out->node=astnew("_Absent",q->t->v[q->pos].span);ok=1;}break;}
    case GX_REP:{LF_Ast*list=astnew("List",q->t->v[q->pos].span);for(;;){size_t st=q->pos;Match m={0};if(!parse_g(q,g->items[0],&m)){q->pos=st;matchfree(&m);break;}if(q->pos==st){matchfree(&m);break;}astadd(list,NULL,m.node);m.node=NULL;mergecaps(&out->caps,&m.caps);}out->node=list;ok=1;break;}
    }
done:q->depth--;return ok;
}

static const char *node_text(const LF_Ast *a) {
    if(!a) return NULL;
    if(a->text) return a->text;
    if(a->n == 1) return node_text(a->children[0]);
    return NULL;
}


static void astadd_flat(LF_Ast *dst, LF_Ast *v) {
    if(!v || strcmp(v->kind,"_Absent")==0) { lf_ast_free(v); return; }
    if(strcmp(v->kind,"List")==0) {
        for(size_t i=0;i<v->n;i++) {
            LF_Ast *c=v->children[i]; v->children[i]=NULL; astadd(dst,NULL,c);
        }
        for(size_t i=0;i<v->n;i++) free(v->fields[i]);
        free(v->children); free(v->fields); free(v->kind); free(v->text); free(v);
        return;
    }
    astadd(dst,NULL,v);
}

static LF_Ast *eval_action(AExpr*a,Caps*c,LF_Span fallback){
    if(!a)return NULL;
    if(a->kind==AX_NULL)return astnew("Nil",fallback);
    if(a->kind==AX_STRING)return asttext("Literal",a->text,fallback);
    if(a->kind==AX_REF){
        Capture*x=capfind(c,a->text);
        if(!x||x->n==0)return astnew("_Absent",fallback);
        if(x->n==1)return astclone(x->vals[0]);
        LF_Ast*l=astnew("List",x->vals[0]->span);
        for(size_t i=0;i<x->n;i++)astadd(l,NULL,astclone(x->vals[i]));
        return l;
    }

    if(strcmp(a->text,"Pass")==0) {
        if(a->nargs!=1) return astnew("_Absent",fallback);
        return eval_action(a->args[0],c,fallback);
    }

    if(strcmp(a->text,"Tail")==0) {
        LF_Ast*n=astnew("_Tail",fallback);
        for(size_t i=0;i<a->nargs;i++) astadd(n,NULL,eval_action(a->args[i],c,fallback));
        return n;
    }

    if(strcmp(a->text,"Fold")==0 && a->nargs==2) {
        LF_Ast *cur=eval_action(a->args[0],c,fallback);
        LF_Ast *rest=eval_action(a->args[1],c,fallback);
        if(strcmp(rest->kind,"_Absent")==0){lf_ast_free(rest);return cur;}
        size_t n = strcmp(rest->kind,"List")==0 ? rest->n : 1;
        for(size_t i=0;i<n;i++) {
            LF_Ast *tail = strcmp(rest->kind,"List")==0 ? rest->children[i] : rest;
            if(!tail || strcmp(tail->kind,"_Tail")!=0 || tail->n<2) continue;
            const char *op=node_text(tail->children[0]);
            LF_Ast *rhs=astclone(tail->children[1]);
            LF_Ast *bin=asttext("Binary",op?op:"?",cur->span);
            astadd(bin,"left",cur); astadd(bin,"right",rhs); astorigin_copy_all(bin,tail); cur=bin;
        }
        lf_ast_free(rest);
        return cur;
    }

    if(strcmp(a->text,"Right")==0 && a->nargs==2) {
        LF_Ast *left=eval_action(a->args[0],c,fallback);
        LF_Ast *tail=eval_action(a->args[1],c,fallback);
        if(strcmp(tail->kind,"_Absent")==0){lf_ast_free(tail);return left;}
        if(strcmp(tail->kind,"_Tail")==0 && tail->n>=2){
            const char *op=node_text(tail->children[0]);
            LF_Ast *rhs=astclone(tail->children[1]);
            LF_Ast *bin=asttext("Binary",op?op:"?",left->span);
            astadd(bin,"left",left);astadd(bin,"right",rhs);astorigin_copy_all(bin,tail);lf_ast_free(tail);return bin;
        }
        lf_ast_free(tail); return left;
    }

    if(strcmp(a->text,"Postfix")==0 && a->nargs==2) {
        LF_Ast *cur=eval_action(a->args[0],c,fallback);
        LF_Ast *tails=eval_action(a->args[1],c,fallback);
        if(strcmp(tails->kind,"_Absent")==0){lf_ast_free(tails);return cur;}
        size_t n=strcmp(tails->kind,"List")==0?tails->n:1;
        for(size_t i=0;i<n;i++){
            LF_Ast *sf=strcmp(tails->kind,"List")==0?tails->children[i]:tails;
            LF_Ast *nn=NULL;
            if(strcmp(sf->kind,"_IndexSuffix")==0 && sf->n>=1){nn=astnew("Index",cur->span);astadd(nn,"base",cur);astadd(nn,"key",astclone(sf->children[0]));}
            else if(strcmp(sf->kind,"_FieldSuffix")==0 && sf->n>=1){nn=astnew("Field",cur->span);astadd(nn,"base",cur);astadd(nn,"name",astclone(sf->children[0]));}
            else if(strcmp(sf->kind,"_CallSuffix")==0){nn=astnew("Call",cur->span);astadd(nn,"callee",cur);if(sf->n)astadd_flat(nn,astclone(sf->children[0]));}
            else if(strcmp(sf->kind,"_MethodSuffix")==0 && sf->n>=1){nn=astnew("MethodCall",cur->span);astadd(nn,"base",cur);astadd(nn,"method",astclone(sf->children[0]));if(sf->n>=2)astadd_flat(nn,astclone(sf->children[1]));}
            if(nn) { astorigin_copy_all(nn,sf); cur=nn; }
        }
        lf_ast_free(tails); return cur;
    }

    if(strcmp(a->text,"IndexSuffix")==0 || strcmp(a->text,"FieldSuffix")==0 ||
       strcmp(a->text,"CallSuffix")==0 || strcmp(a->text,"MethodSuffix")==0) {
        char kind[32]; snprintf(kind,sizeof(kind),"_%s",a->text);
        LF_Ast*n=astnew(kind,fallback);
        for(size_t i=0;i<a->nargs;i++) {
            LF_Ast *v=eval_action(a->args[i],c,fallback);
            astorigin_copy_all(n,v);
            astadd_flat(n,v);
        }
        return n;
    }

    if(strcmp(a->text,"Unary")==0 && a->nargs==2) {
        LF_Ast *opv=eval_action(a->args[0],c,fallback);
        LF_Ast *value=eval_action(a->args[1],c,fallback);
        const char *op=node_text(opv);
        LF_Ast *n=asttext("Unary",op?op:"?",opv->span);
        astorigin_copy_all(n,opv);
        astadd(n,"value",value);
        lf_ast_free(opv);
        return n;
    }

    /* Lexical leaves must preserve byte length; Lua strings may contain NUL. */
    if((strcmp(a->text,"Name")==0 || strcmp(a->text,"Number")==0 || strcmp(a->text,"String")==0) && a->nargs==1){
        LF_Ast*v=eval_action(a->args[0],c,fallback);
        const char*t=v->text?v->text:""; size_t z=v->text?v->text_len:0;
        LF_Ast*n=asttextn(a->text,t,z,v->span); lf_ast_free(v); return n;
    }
    if(strcmp(a->text,"Bool")==0 && a->nargs==1){
        LF_Ast*v=eval_action(a->args[0],c,fallback); const char*t=node_text(v);
        LF_Ast*n=asttext(a->text,t?t:"",v->span); lf_ast_free(v); return n;
    }

    LF_Ast*n=astnew(a->text,fallback);
    for(size_t i=0;i<a->nargs;i++){
        LF_Ast*v=eval_action(a->args[i],c,fallback);
        if(strcmp(a->text,"Block")==0 || strcmp(a->text,"ExprList")==0 || strcmp(a->text,"VarList")==0 || strcmp(a->text,"ParamList")==0 || strcmp(a->text,"List")==0 || strcmp(a->text,"DeclList")==0 || strcmp(a->text,"FuncName")==0 || strcmp(a->text,"If")==0)
            astadd_flat(n,v);
        else if(strcmp(v->kind,"_Absent")==0) lf_ast_free(v);
        else astadd(n,NULL,v);
    }
    /* The production span includes syntax-only terminals that are intentionally
       absent from the canonical children (for example a function's final
       `end`).  Keep that full span on the semantic node so the backend can
       reconstruct compiler/debug line locations without retaining syntax
       tokens in the canonical AST. */
    n->span=fallback;
    return n;
}

static void collect_direct_literal_origins(LF_Ast *dst,const LF_Ast *raw){
    if(!dst||!raw)return;
    if(strcmp(raw->kind,"Token")==0){
        if(raw->text) astorigin_add(dst,raw->text,raw->text_len,raw->span);
        return;
    }
    if(strcmp(raw->kind,"Seq")!=0 && strcmp(raw->kind,"List")!=0 &&
       strcmp(raw->kind,"_Absent")!=0) return;
    for(size_t i=0;i<raw->n;i++)collect_direct_literal_origins(dst,raw->children[i]);
}

static int parse_rule(Parser*q,Rule*r,Match*out){
    size_t st=q->pos;
    Match m={0};
    if(!parse_g(q,r->expr,&m)){
        q->pos=st;
        matchfree(&m);
        return 0;
    }
    if(r->action){
        LF_Span sp=m.node?m.node->span:q->t->v[st].span;
        LF_Ast*raw=m.node;
        LF_Ast*n=eval_action(r->action,&m.caps,sp);
        collect_direct_literal_origins(n,raw);
        lf_ast_free(raw);
        m.node=n;
    }
    /* Captures are lexical to a production.  A caller sees only the
       production value and can capture that value explicitly. */
    capfree(&m.caps);
    memset(&m.caps,0,sizeof(m.caps));
    *out=m;
    return 1;
}

static int is_lvalue(const LF_Ast *a) {
    return a && (strcmp(a->kind,"Name")==0 || strcmp(a->kind,"Index")==0 || strcmp(a->kind,"Field")==0);
}
static int is_statement(const LF_Ast *a) {
    static const char *k[]={"Empty","Assign","Local","Global","GlobalAll","FunctionDecl","LocalFunction","GlobalFunction","If","While","Repeat","NumericFor","GenericFor","Return","Break","Goto","Label","Call","MethodCall","Do"};
    if(!a) return 0;
    for(size_t i=0;i<sizeof(k)/sizeof(k[0]);i++) if(strcmp(a->kind,k[i])==0) return 1;
    return 0;
}
static int validate_canonical(const LF_Ast *a, int in_block, LF_Error *err) {
    if(!a) return 1;
    if(in_block && !is_statement(a)) {
        seterrk(err,LF_ERROR_CANONICAL,a->span,"canonical Lua error: '%s' is not a valid statement",a->kind);
        return 0;
    }
    if(strcmp(a->kind,"Assign")==0 && a->n>0) {
        const LF_Ast *vars=a->children[0];
        if(strcmp(vars->kind,"VarList")==0) {
            for(size_t i=0;i<vars->n;i++) if(!is_lvalue(vars->children[i])) {
                seterrk(err,LF_ERROR_CANONICAL,vars->children[i]->span,"canonical Lua error: assignment target is not a variable");
                return 0;
            }
        }
    }
    if(strcmp(a->kind,"Block")==0) {
        for(size_t i=0;i<a->n;i++) if(!validate_canonical(a->children[i],1,err)) return 0;
        return 1;
    }
    for(size_t i=0;i<a->n;i++) if(!validate_canonical(a->children[i],0,err)) return 0;
    return 1;
}

int lf_profile_has_rule(const LF_Profile *p, const char *rule_name) {
    return p && rule_name && find_rule((LF_Profile *)p, rule_name) != NULL;
}

LF_Ast *lf_parse_rule(LF_Profile*p,const char*rule_name,const char*source_name,
                      const char*source,size_t source_len,LF_Error*err){
    if (err) memset(err, 0, sizeof(*err));
    if (!p || !rule_name || !source) {
        LF_Span sp={source_name,0,0,1,1,1,1};
        seterrk(err,LF_ERROR_SYNTAX,sp,"invalid parser arguments");
        return NULL;
    }
    Rule *r=find_rule(p,rule_name);
    if (!r) {
        LF_Span sp={source_name,0,0,1,1,1,1};
        seterrk(err,LF_ERROR_PROFILE,sp,"profile '%s' has no rule '%s'",
                p->name?p->name:"?",rule_name);
        return NULL;
    }
    Tokens t={0};
    if(!lex_source(p,source_name,source,source_len,&t,err)){toksfree(&t);return NULL;}
    Parser q={p,&t,0,err,0,0,NULL,0,0};Match m={0};
    if(!parse_rule(&q,r,&m)){
        size_t pos=q.limit_hit?q.limit_pos:(q.farthest<t.n?q.farthest:t.n-1);
        if(pos>=t.n) pos=t.n-1;
        LF_Span sp=t.v[pos].span;
        LF_ErrorKind kind=(t.v[pos].k==ST_EOF)?LF_ERROR_INCOMPLETE:LF_ERROR_SYNTAX;
        if(q.limit_hit) seterrk(err,kind,sp,"too many syntax levels");
        else seterrk(err,kind,sp,"syntax error: expected %s near '%s'",
                q.expected?q.expected:"input",t.v[pos].text);
        matchfree(&m);toksfree(&t);return NULL;
    }
    if(q.pos<t.n-1){
        LF_Span sp=t.v[q.pos].span;
        seterrk(err,LF_ERROR_SYNTAX,sp,"syntax error: trailing input near '%s'",t.v[q.pos].text);
        matchfree(&m);toksfree(&t);return NULL;
    }
    capfree(&m.caps);toksfree(&t);
    if(!validate_canonical(m.node,0,err)){lf_ast_free(m.node);return NULL;}
    return m.node;
}

LF_Ast *lf_parse(LF_Profile*p,const char*source_name,const char*source,size_t source_len,LF_Error*err){
    return lf_parse_rule(p,"chunk",source_name,source,source_len,err);
}

static void dumpesc(FILE *out, const char *s, size_t n) {
    fputc('"', out);
    for (size_t i = 0; s && i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '\\': fputs("\\\\", out); break;
            case '"': fputs("\\\"", out); break;
            case '\0': fputs("\\0", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (c < 0x20 || c == 0x7f) fprintf(out, "\\x%02X", c);
                else fputc(c, out);
        }
    }
    fputc('"', out);
}
static void dumpast(const LF_Ast*a,FILE*out,int ind){for(int i=0;i<ind;i++)fputs("  ",out);fputs(a->kind,out);if(a->text){fputc('(',out);dumpesc(out,a->text,a->text_len);fputc(')',out);}fprintf(out," @%u:%u-%u:%u",a->span.line_start,a->span.col_start,a->span.line_end,a->span.col_end);if(a->n)fputs(" {",out);fputc('\n',out);for(size_t i=0;i<a->n;i++){if(a->fields[i]){for(int j=0;j<ind+1;j++)fputs("  ",out);fprintf(out,"%s:\n",a->fields[i]);dumpast(a->children[i],out,ind+2);}else dumpast(a->children[i],out,ind+1);}if(a->n){for(int i=0;i<ind;i++)fputs("  ",out);fputs("}\n",out);}}
void lf_ast_dump(const LF_Ast*a,FILE*out){if(a)dumpast(a,out,0);}

const char *lf_ast_kind(const LF_Ast *a){return a?a->kind:NULL;}
const char *lf_ast_text(const LF_Ast *a){return a?a->text:NULL;}
size_t lf_ast_text_len(const LF_Ast *a){return a?a->text_len:0;}
LF_Span lf_ast_span(const LF_Ast *a){LF_Span z={0};return a?a->span:z;}
size_t lf_ast_child_count(const LF_Ast *a){return a?a->n:0;}
const LF_Ast *lf_ast_child(const LF_Ast *a,size_t i){return (a&&i<a->n)?a->children[i]:NULL;}
const char *lf_ast_child_field(const LF_Ast *a,size_t i){return (a&&i<a->n)?a->fields[i]:NULL;}
int lf_ast_literal_span(const LF_Ast *a,const char *literal,size_t occurrence,LF_Span *out){
    if(!a||!literal)return 0;
    size_t seen=0;size_t z=strlen(literal);
    for(size_t i=0;i<a->norigins;i++){AstLiteralOrigin *o=&a->origins[i];if(o->len==z&&memcmp(o->text,literal,z)==0){if(seen++==occurrence){if(out)*out=o->span;return 1;}}}
    return 0;
}
