#ifndef MXYSTDLIB_H
#define MXYSTDLIB_H

typedef struct {
    const char *path;
    const char *source;
} StdlibEntry;

const char *stdlib_lookup(const char *path);

#endif
