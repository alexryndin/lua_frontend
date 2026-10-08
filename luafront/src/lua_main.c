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
                                int *noenv, const char **error,
                                const char **explicit_map) {
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
        if ((strcmp(a, "-e") == 0 || strcmp(a, "-l") == 0 ||
             strcmp(a, "--syntax-map") == 0) && i + 1 < argc) {
            if (strcmp(a, "--syntax-map") == 0) {
                if (*explicit_map) {
                    *error = "duplicate --syntax-map option";
                    free(v);
                    return 0;
                }
                *explicit_map = argv[i + 1];
                i++;
                continue;
            }
            v[n++] = argv[i];
            v[n++] = argv[++i];
            continue;
        }
        if (strncmp(a, "-e", 2) == 0 || strncmp(a, "-l", 2) == 0) {
            v[n++] = argv[i];
            continue;
        }
        if (strncmp(a, "--syntax-map=", 13) == 0) {
            if (*explicit_map || a[13] == '\0') {
                *error = "invalid or duplicate --syntax-map option";
                free(v);
                return 0;
            }
            *explicit_map = a + 13;
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

/* Parse one syntax-map entry "ext=name:path".  Entries map a real file
   extension to the profile declared as `name` in `path`. */
static int parse_map_entry(const char *s, LF_CliSyntaxMap *out,
                           const char **error) {
    const char *eq = strchr(s, '=');
    if (eq == NULL || eq == s) { *error = "syntax map entry must be ext=name:path"; return 0; }
    size_t extlen = (size_t)(eq - s);
    if (extlen == 0 || extlen >= sizeof(out->ext)) { *error = "syntax map extension too long or empty"; return 0; }
    for (size_t i = 0; i < extlen; i++) {
        char c = s[i];
        if (c == '.' || c == '/' || c == ',') { *error = "invalid syntax map extension"; return 0; }
    }
    if (extlen == 3 && strncmp(s, "lua", 3) == 0) { *error = "extension 'lua' is reserved for builtin lua55"; return 0; }
    memcpy(out->ext, s, extlen);
    out->ext[extlen] = '\0';

    const char *rest = eq + 1;
    const char *colon = strchr(rest, ':');
    if (colon == NULL || colon == rest) { *error = "syntax map entry must be ext=name:path"; return 0; }
    size_t namelen = (size_t)(colon - rest);
    if (namelen == 0 || namelen >= sizeof(out->name)) { *error = "syntax map profile name empty or too long"; return 0; }
    if (namelen == 5 && strncmp(rest, "lua55", 5) == 0) { *error = "syntax profile name 'lua55' is reserved for the builtin"; return 0; }
    memcpy(out->name, rest, namelen);
    out->name[namelen] = '\0';

    size_t pathlen = strlen(colon + 1);
    if (pathlen == 0 || pathlen >= sizeof(out->path)) { *error = "syntax map path is empty or too long"; return 0; }
    memcpy(out->path, colon + 1, pathlen);
    out->path[pathlen] = '\0';
    return 1;
}

/* Add a map entry; CLI entries override env entries for the same
   extension, duplicates within one source are an error. */
static int add_map_entry(LF_CliSyntaxMap *maps, size_t *nmaps,
                         const LF_CliSyntaxMap *e, int replace_same_ext,
                         const char **error) {
    for (size_t i = 0; i < *nmaps; i++) {
        if (strcmp(maps[i].ext, e->ext) == 0) {
            if (!replace_same_ext) {
                *error = "duplicate syntax mapping for the same extension";
                return 0;
            }
            maps[i] = *e;  /* CLI overrides env */
            return 1;
        }
    }
    if (*nmaps >= 64) { *error = "too many syntax mappings"; return 0; }
    maps[*nmaps] = *e;
    (*nmaps)++;
    return 1;
}

static int build_syntax_config(int noenv, LF_CliSyntaxMap *maps,
                               size_t *nmaps, const char **error) {
    *nmaps = 0;
    if (!noenv) {
        const char *env = getenv("LUA_SYNTAX_MAP");
        if (env && *env) {
            char envbuf[2048];
            if (strlen(env) >= sizeof(envbuf)) { *error = "LUA_SYNTAX_MAP is too long"; return 0; }
            strcpy(envbuf, env);
            char *p = envbuf;
            while (*p) {
                char *comma = strchr(p, ',');
                if (comma) *comma = '\0';
                if (*p) {
                    LF_CliSyntaxMap e;
                    if (!parse_map_entry(p, &e, error)) return 0;
                    if (!add_map_entry(maps, nmaps, &e, 0, error)) return 0;
                }
                p = comma ? comma + 1 : p + strlen(p);
            }
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    char **puc_argv = NULL;
    const char *explicit_path = NULL;
    const char *explicit_map = NULL;
    const char *option_error = NULL;
    int native = 0;
    int noenv = 0;
    int puc_argc = filter_front_options(argc, argv, &puc_argv,
                                        &explicit_path, &native, &noenv,
                                        &option_error, &explicit_map);
    if (puc_argc == 0)
        return fail_option(argv[0], option_error ? option_error : "invalid option");

    LF_CliSyntaxMap maps[64];
    size_t nmaps = 0;
    if (!build_syntax_config(noenv, maps, &nmaps, &option_error))
        return fail_option(argv[0], option_error);
    if (explicit_map) {
        LF_CliSyntaxMap e;
        if (!parse_map_entry(explicit_map, &e, &option_error))
            return fail_option(argv[0], option_error);
        if (!add_map_entry(maps, &nmaps, &e, 1, &option_error))
            return fail_option(argv[0], option_error);
    }

    /* --native: no compiler hook at all, maps and resolver do not exist */
    LF_Profile *entry_profile = NULL;
    if (!native) {
        LF_Error err = {0};
        entry_profile = lf_profile_discover(explicit_path, noenv, &err);
        if (entry_profile == NULL) {
            print_front_error(&err);
            free(puc_argv);
            return EXIT_FAILURE;
        }
        if (lf_profile_native_diag(entry_profile)) {
            /* nothing selected: discovery fell back to the builtin; the
               builtin is the state default anyway, no override needed */
            lf_profile_free(entry_profile);
            entry_profile = NULL;
        }
    }

    lf_cli_set_syntax_config(entry_profile, maps, nmaps);
    int status = puc_lua_main(puc_argc, puc_argv);
    free(puc_argv);
    return status;
}
