#include "luafront_lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *readall(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long z = ftell(f);
    if (z < 0) { fclose(f); return NULL; }
    rewind(f);
    char *b = (char *)malloc((size_t)z + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)z, f) != (size_t)z) {
        free(b); fclose(f); return NULL;
    }
    fclose(f);
    b[z] = '\0';
    /* Match luaL_loadfile: a first line beginning with '#' is a Unix
       launcher line, not Lua source. Preserve its width/newline so all
       source spans and compiler line numbers stay aligned. */
    if (z > 0 && b[0] == '#') {
        size_t i = 0;
        while (i < (size_t)z && b[i] != '\n' && b[i] != '\r') b[i++] = ' ';
    }
    *n = (size_t)z;
    return b;
}

static void frontend_error(const LF_Error *e) {
    if (e && e->span.file && e->span.line_start)
        fprintf(stderr, "%s:%u:%u: %s\n", e->span.file,
                e->span.line_start, e->span.col_start, e->message);
    else if (e && e->message[0])
        fprintf(stderr, "%s\n", e->message);
}

static int run_file(const char *profile_path, const char *source_path, int execute, int native) {
    LF_Error e = {0};
    LF_Profile *profile = NULL;
    if (!native) {
        profile = lf_profile_discover(profile_path, 0, &e);
        if (!profile) { frontend_error(&e); return 1; }
    }

    size_t n = 0;
    char *source = readall(source_path, &n);
    if (!source) {
        fprintf(stderr, "%s: %s\n", source_path, strerror(errno));
        lf_profile_free(profile);
        return 1;
    }

    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "luax: cannot create Lua state\n");
        free(source); lf_profile_free(profile); return 1;
    }
    luaL_openlibs(L);
    if (!native) lf_lua_install_profile(L, profile);

    /* Use the normal Lua loading API. The installed source-compiler hook
       routes this and every nested load/loadfile/require through profile. */
    int status = luaL_loadbufferx(L, source, n, source_path, "t");
    free(source);

    if (status != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        if (msg) fprintf(stderr, "%s\n", msg);
        else frontend_error(&e);
        if (!native) lf_lua_uninstall_profile(L);
        lua_close(L);
        lf_profile_free(profile);
        return 1;
    }

    if (execute) {
        status = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (status != LUA_OK) {
            const char *msg = lua_tostring(L, -1);
            fprintf(stderr, "%s\n", msg ? msg : "runtime error");
            if (!native) lf_lua_uninstall_profile(L);
            lua_close(L);
            lf_profile_free(profile);
            return 1;
        }
    }

    if (!native) lf_lua_uninstall_profile(L);
    lua_close(L);
    lf_profile_free(profile);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "--syntax") == 0)
        return run_file(argv[2], argv[3], 1, 0);
    if (argc == 5 && strcmp(argv[1], "--check") == 0 && strcmp(argv[2], "--syntax") == 0)
        return run_file(argv[3], argv[4], 0, 0);
    if (argc == 3 && strcmp(argv[1], "--native") == 0)
        return run_file(NULL, argv[2], 1, 1);
    if (argc == 4 && strcmp(argv[1], "--check") == 0 && strcmp(argv[2], "--native") == 0)
        return run_file(NULL, argv[3], 0, 1);
    if (argc == 3 && strcmp(argv[1], "--check") == 0) {
        return run_file(NULL, argv[2], 0, 0);
    }
    if (argc == 2) {
        return run_file(NULL, argv[1], 1, 0);
    }
    fprintf(stderr,
            "usage:\n"
            "  %s SOURCE\n"
            "  %s --syntax PROFILE SOURCE\n"
            "  %s --native SOURCE\n"
            "  %s --check SOURCE\n"
            "  %s --check --syntax PROFILE SOURCE\n"
            "  %s --check --native SOURCE\n",
            argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
    return 2;
}
