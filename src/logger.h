#ifndef XPMOVER_LOGGER_H
#define XPMOVER_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

#define MVLOG_LEVEL_ERROR 127
#define MVLOG_LEVEL_WARN  63
#define MVLOG_LEVEL_INFO  31
#define MVLOG_LEVEL_DEBUG 15
#define MVLOG_LEVEL_TRACE 7

#ifndef MVLOG_COMPILED_MIN_LOG_LEVEL
#define MVLOG_COMPILED_MIN_LOG_LEVEL MVLOG_LEVEL_INFO
#endif

#if (MVLOG_COMPILED_MIN_LOG_LEVEL <= MVLOG_LEVEL_ERROR)
#define MVLOG_ERROR(format, ...) xpmover_log(MVLOG_LEVEL_ERROR, format __VA_OPT__(,) __VA_ARGS__)
#else
#define MVLOG_ERROR(format, ...)
#endif

#if (MVLOG_COMPILED_MIN_LOG_LEVEL <= MVLOG_LEVEL_WARN)
#define MVLOG_WARN(format, ...) xpmover_log(MVLOG_LEVEL_WARN, format __VA_OPT__(,) __VA_ARGS__)
#else
#define MVLOG_WARN(format, ...)
#endif

#if (MVLOG_COMPILED_MIN_LOG_LEVEL <= MVLOG_LEVEL_INFO)
#define MVLOG_INFO(format, ...) xpmover_log(MVLOG_LEVEL_INFO, format __VA_OPT__(,) __VA_ARGS__)
#else
#define MVLOG_INFO(format, ...)
#endif

#if (MVLOG_COMPILED_MIN_LOG_LEVEL <= MVLOG_LEVEL_DEBUG)
#define MVLOG_DEBUG(format, ...) xpmover_log(MVLOG_LEVEL_DEBUG, format __VA_OPT__(,) __VA_ARGS__)
#else
#define MVLOG_DEBUG(format, ...)
#endif

#if (MVLOG_COMPILED_MIN_LOG_LEVEL <= MVLOG_LEVEL_TRACE)
#define MVLOG_TRACE(format, ...) xpmover_log(MVLOG_LEVEL_TRACE, format __VA_OPT__(,) __VA_ARGS__)
#else
#define MVLOG_TRACE(format, ...)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t xpmover_log_level_t;

void xpmover_log_init();
void xpmover_log_destroy();

__attribute__((__format__ (__printf__, 2, 3)))
void xpmover_log(xpmover_log_level_t level, const char *format, ...);

void xpmover_set_min_log_level_console(xpmover_log_level_t level);
void xpmover_set_min_log_level_xplane(xpmover_log_level_t level);

xpmover_log_level_t xpmover_get_min_log_level_console();
xpmover_log_level_t xpmover_get_min_log_level_xplane();

bool xpmover_is_log_level_enabled(xpmover_log_level_t level);

#if (MVLOG_COMPILED_MIN_LOG_LEVEL <= MVLOG_LEVEL_TRACE)
#define MVLOG_IS_TRACE_ENABLED() xpmover_is_log_level_enabled(MVLOG_LEVEL_TRACE)
#else
#define MVLOG_IS_TRACE_ENABLED() (false)
#endif

#ifdef __cplusplus
}
#endif

#endif
