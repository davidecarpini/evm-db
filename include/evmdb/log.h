#ifndef EVMDB_LOG_H
#define EVMDB_LOG_H

typedef enum {
    EVMDB_LOG_ERROR = 0,
    EVMDB_LOG_WARN  = 1,
    EVMDB_LOG_INFO  = 2,
    EVMDB_LOG_DEBUG = 3,
} evmdb_log_level_t;

void evmdb_log_init(evmdb_log_level_t level);

void evmdb_log_write(evmdb_log_level_t level, const char *file, int line,
                     const char *fmt, ...);

#define LOG_ERROR(...) evmdb_log_write(EVMDB_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  evmdb_log_write(EVMDB_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  evmdb_log_write(EVMDB_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) evmdb_log_write(EVMDB_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

#endif /* EVMDB_LOG_H */
