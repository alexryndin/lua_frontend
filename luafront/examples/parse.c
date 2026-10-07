#include "luafront.h"

#include <stdio.h>

int main(void) {
    static const char source[] = "return 40 + 2";
    LF_Error err = {0};
    LF_Profile *profile = lf_profile_builtin_lua55(&err);
    if (profile == NULL) {
        fprintf(stderr, "%s\n", err.message);
        return 1;
    }
    LF_Ast *ast = lf_parse(profile, "=example", source,
                           sizeof(source) - 1, &err);
    if (ast == NULL) {
        fprintf(stderr, "%s\n", err.message);
        lf_profile_free(profile);
        return 1;
    }
    printf("profile=%s version=%s root=%s\n",
           lf_profile_name(profile),
           lf_profile_version(profile) ? lf_profile_version(profile) : "-",
           lf_ast_kind(ast));
    lf_ast_free(ast);
    lf_profile_free(profile);
    return 0;
}
