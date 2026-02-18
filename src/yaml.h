#ifndef MOXY_YAML_H
#define MOXY_YAML_H

typedef struct {
    int indent;
    int brace_knr;
    int space_around_ops;
    int space_after_comma;
    int space_after_keyword;
    int trailing_newline;
    int max_line_length;
    int lint_unused_vars;
    int lint_empty_blocks;
    int lint_shadow_vars;
} MoxyConfig;

MoxyConfig config_defaults(void);
MoxyConfig config_load(const char *path);
char *config_find(const char *start_dir, const char *file_dir);

typedef struct {
    char cflags[512];
    char ldflags[512];
} GooseBuild;

GooseBuild goose_load(const char *path);
char *goose_find(const char *file_dir);

#endif
