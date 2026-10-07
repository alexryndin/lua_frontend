#include "lua_cli_ext.h"
#include "luafront.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int puc_lua_main(int argc, char **argv);

static void print_front_error(const LF_Error *e) {
    if (e != NULL && e->span.file != NULL && e->span.line_start != 0)
        fprintf(stderr, "%s:%u:%u: %s\n", e->span.file,
                e->span.line_start, e->span.col_start, e->message);
    else if (e != NULL && e->message[0] != '\0')
        fprintf(stderr, "%s\n", e->message);
}

static int fail_option(const char *prog, const char *msg) {
    fprintf(stderr, "%s: %s\n", prog ? prog : "lua", msg);
    fprintf(stderr, "usage: %s [--syntax file | --native] [lua options] [script [args]]\n",
            prog ? prog : "lua");
    return EXIT_FAILURE;
}

/*
** Remove luafront-only options before handing argv to the stock PUC CLI.
** Parsing stops at the same places as PUC's own option parser. Arguments to
** -e/-l are copied verbatim and are never interpreted as luafront options.
*/
static int filter_front_options(int argc, char **argv, char ***out_argv,
                                const char **explicit_path, int *native,
                                int *noenv, const char **error) {
    char **v = (char **)calloc((size_t)argc + 1, sizeof(*v));
    int n = 0;
    int stop = 0;
    if (v == NULL) abort();

    v[n++] = argv[0];
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (stop) {
            v[n++] = argv[i];
            continue;
        }
        if (strcmp(a, "--") == 0 || strcmp(a, "-") == 0 || a[0] != '-') {
            v[n++] = argv[i];
            stop = 1;
            continue;
        }
        if (strcmp(a, "-E") == 0) {
            *noenv = 1;
            v[n++] = argv[i];
            continue;
        }
        if ((strcmp(a, "-e") == 0 || strcmp(a, "-l") == 0) && i + 1 < argc) {
            v[n++] = argv[i];
            v[n++] = argv[++i];
            continue;
        }
        if (strncmp(a, "-e", 2) == 0 || strncmp(a, "-l", 2) == 0) {
            v[n++] = argv[i];
            continue;
        }
        if (strcmp(a, "--native") == 0) {
            if (*native || *explicit_path != NULL) {
                *error = "--native conflicts with another syntax selection";
                free(v);
                return 0;
            }
            *native = 1;
            continue;
        }
        if (strncmp(a, "--syntax=", 9) == 0) {
            if (*native || *explicit_path != NULL || a[9] == '\0') {
                *error = "invalid or duplicate --syntax option";
                free(v);
                return 0;
            }
            *explicit_path = a + 9;
            continue;
        }
        if (strcmp(a, "--syntax") == 0) {
            if (*native || *explicit_path != NULL || i + 1 >= argc) {
                *error = "--syntax needs exactly one profile path";
                free(v);
                return 0;
            }
            *explicit_path = argv[++i];
            continue;
        }
        v[n++] = argv[i];
    }
    v[n] = NULL;
    *out_argv = v;
    return n;
}

int main(int argc, char **argv) {
    char **puc_argv = NULL;
    const char *explicit_path = NULL;
    const char *option_error = NULL;
    int native = 0;
    int noenv = 0;
    int puc_argc = filter_front_options(argc, argv, &puc_argv,
                                        &explicit_path, &native, &noenv,
                                        &option_error);
    if (puc_argc == 0)
        return fail_option(argv[0], option_error ? option_error : "invalid option");

    LF_Profile *profile = NULL;
    if (!native) {
        LF_Error err = {0};
        profile = lf_profile_discover(explicit_path, noenv, &err);
        if (profile == NULL) {
            print_front_error(&err);
            free(puc_argv);
            return EXIT_FAILURE;
        }
    }

    lf_cli_set_profile(profile);
    int status = puc_lua_main(puc_argc, puc_argv);
    lf_cli_set_profile(NULL);
    lf_profile_free(profile);
    free(puc_argv);
    return status;
}
