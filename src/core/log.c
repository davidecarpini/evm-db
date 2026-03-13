#include "evmdb/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

static evmdb_log_level_t g_level = EVMDB_LOG_INFO;

static const char *level_str[] = {
    "ERROR", "WARN ", "INFO ", "DEBUG",
};

static const char *level_color[] = {
    "\033[31m", "\033[33m", "\033[32m", "\033[36m",
};

void evmdb_log_init(evmdb_log_level_t level) {
    g_level = level;
}

void evmdb_log_write(evmdb_log_level_t level, const char *file, int line,
                     const char *fmt, ...) {
    if (level > g_level) {
        return;
    }

    /* Timestamp */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    /* Strip path, keep filename only */
    const char *basename = strrchr(file, '/');
    basename = basename ? basename + 1 : file;

    fprintf(stderr, "%s%02d:%02d:%02d.%03ld [%s]\033[0m %s:%d: ",
            level_color[level],
            tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000,
            level_str[level],
            basename, line);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
}
