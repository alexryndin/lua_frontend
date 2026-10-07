#include "luafront.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[size] = '\0';
    *len = (size_t)size;
    return buf;
}

static void print_error(const LF_Error *err) {
    if (err->span.file != NULL)
        fprintf(stderr, "%s:%u:%u: %s\n", err->span.file,
                err->span.line_start, err->span.col_start, err->message);
    else
        fprintf(stderr, "%s\n", err->message);
}

static int parse_source(const char *profile_path, const char *source_path,
                        int dump_tokens) {
    LF_Error err = {0};
    LF_Profile *profile = lf_profile_discover(profile_path, 0, &err);
    if (profile == NULL) {
        print_error(&err);
        return 1;
    }

    size_t source_len = 0;
    char *source = read_all(source_path, &source_len);
    if (source == NULL) {
        fprintf(stderr, "%s: %s\n", source_path, strerror(errno));
        lf_profile_free(profile);
        return 1;
    }

    LF_Ast *ast = lf_parse(profile, source_path, source, source_len, &err);
    free(source);
    if (ast == NULL) {
        print_error(&err);
        lf_profile_free(profile);
        return 1;
    }

    int status = 0;
    if (dump_tokens) {
        LF_CanonTokens *tokens = lf_ast_to_canonical_tokens(ast, &err);
        if (tokens == NULL) {
            print_error(&err);
            status = 1;
        }
        else {
            lf_canonical_tokens_dump(tokens, stdout);
            lf_canonical_tokens_free(tokens);
        }
    }
    else {
        lf_ast_dump(ast, stdout);
    }

    lf_ast_free(ast);
    lf_profile_free(profile);
    return status;
}

static int check_profile(const char *path) {
    LF_Error err = {0};
    LF_Profile *profile = lf_profile_load(path, &err);
    if (profile == NULL) {
        print_error(&err);
        return 1;
    }
    const char *version = lf_profile_version(profile);
    if (version != NULL)
        printf("profile %s: OK (%zu rules, version %s)\n",
               lf_profile_name(profile), lf_profile_rule_count(profile), version);
    else
        printf("profile %s: OK (%zu rules)\n",
               lf_profile_name(profile), lf_profile_rule_count(profile));
    lf_profile_free(profile);
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "usage:\n"
            "  %s SOURCE\n"
            "  %s --syntax PROFILE SOURCE\n"
            "  %s --tokens --syntax PROFILE SOURCE\n"
            "  %s --check-profile PROFILE\n",
            prog, prog, prog, prog);
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--check-profile") == 0)
        return check_profile(argv[2]);
    if (argc == 4 && strcmp(argv[1], "--syntax") == 0)
        return parse_source(argv[2], argv[3], 0);
    if (argc == 5 && strcmp(argv[1], "--tokens") == 0 &&
        strcmp(argv[2], "--syntax") == 0)
        return parse_source(argv[3], argv[4], 1);
    if (argc == 2)
        return parse_source(NULL, argv[1], 0);
    usage(argv[0]);
    return 2;
}
