#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "threads_compat.h"

#include "logger.h"

#ifdef HAVE_XPLM
#include <XPLMUtilities.h>
#endif

// FIXME: support longer messages?
// FIXME: in case of memory allocation errors we may not be able to allocate a dynamic buffer in the logger; pre-allocate shared buffer and use while mutex is available?
#define MVLOG_MESSAGE_BUFFER_SIZE 4096 /* must fit at least log prefix with time and level indication */

static xpmover_log_level_t min_log_level_console = MVLOG_COMPILED_MIN_LOG_LEVEL;

#if (MVLOG_COMPILED_MIN_LOG_LEVEL <= MVLOG_LEVEL_INFO)
#define DEFAULT_MIN_LOG_LEVEL_XPLANE MVLOG_LEVEL_INFO
#else
#define DEFAULT_MIN_LOG_LEVEL_XPLANE MVLOG_COMPILED_MIN_LOG_LEVEL
#endif
static xpmover_log_level_t min_log_level_xplane = DEFAULT_MIN_LOG_LEVEL_XPLANE;

static xpmover_log_level_t min_log_level = MVLOG_COMPILED_MIN_LOG_LEVEL;

#ifdef TARGET_WINDOWS
#define LINE_END "\r\n"
#define LINE_END_LENGTH 2
#else
#define LINE_END "\n"
#define LINE_END_LENGTH 1
#endif

#define MVLOG_PREFIX "[Mover] "

#define NANO_PER_MILLI 1000000

static const char *log_fallback_prefix = MVLOG_PREFIX "[no time] ";

static mtx_t log_mutex = {0};
static bool has_log_mutex = false;

void xpmover_log_init() {
    if (has_log_mutex) {
        return;
    }

    if (mtx_init(&log_mutex, mtx_plain) != thrd_success) {
        printf(MVLOG_PREFIX "failed to initialize log mutex" LINE_END);
        return;
    }

    has_log_mutex = true;
}

void xpmover_log_destroy() {
    if (!has_log_mutex) {
        return;
    }

    mtx_destroy(&log_mutex);
    has_log_mutex = false;
}

static void set_min_log_level(xpmover_log_level_t *target, xpmover_log_level_t level) {
    // limit to compiled levels
    if (level < MVLOG_COMPILED_MIN_LOG_LEVEL) {
        level = MVLOG_COMPILED_MIN_LOG_LEVEL;
    }
    
    *target = level;

    // update global minimum log level which is the lowest of all selected levels
    xpmover_log_level_t global_minimum = min_log_level_console;
    if (global_minimum > min_log_level_xplane) {
        global_minimum = min_log_level_xplane;
    }

    min_log_level = global_minimum;
}

void xpmover_set_min_log_level_console(xpmover_log_level_t level) {
    set_min_log_level(&min_log_level_console, level);
}

void xpmover_set_min_log_level_xplane(xpmover_log_level_t level) {
    set_min_log_level(&min_log_level_xplane, level);
}

bool xpmover_is_log_level_enabled(xpmover_log_level_t level) {
    return level >= MVLOG_COMPILED_MIN_LOG_LEVEL && (level >= min_log_level_xplane || level >= min_log_level_console);
}

static const char* log_level_name_message_part(xpmover_log_level_t level) {
    switch (level) {
        case MVLOG_LEVEL_TRACE: return "[TRACE] ";
        case MVLOG_LEVEL_DEBUG: return "[DEBUG] ";
        case MVLOG_LEVEL_INFO:  return "[INFO]  ";
        case MVLOG_LEVEL_WARN:  return "[WARN]  ";
        case MVLOG_LEVEL_ERROR: return "[ERROR] ";
        default:                return "[UNKNW] ";
    }
}

__attribute__((__format__ (__printf__, 3, 0)))
static bool format_message(char *buffer, xpmover_log_level_t level, char *format, va_list args) {
    // buffer needs to have MVLOG_MESSAGE_BUFFER_SIZE
    
    char *buffer_write_pointer = &buffer[0];
    size_t remaining_buffer_bytes = MVLOG_MESSAGE_BUFFER_SIZE - LINE_END_LENGTH - 1;

    // prefix the log message, ideally with a local timestamp
    bool prefixed_with_time = false;
    struct timespec now = {0};
    if (timespec_get(&now, TIME_UTC)) {
        struct tm *now_local = localtime(&now.tv_sec);
        if (now_local) {
            int res = snprintf(buffer_write_pointer, remaining_buffer_bytes, MVLOG_PREFIX "[%02d:%02d:%02d.%03ld] ", now_local->tm_hour, now_local->tm_min, now_local->tm_sec, now.tv_nsec / NANO_PER_MILLI);

            if (res > 0) {
                prefixed_with_time = true;
                buffer_write_pointer += res;
                remaining_buffer_bytes -= res;
            }
        }
    }
    
    if (!prefixed_with_time) {
        size_t length = strlen(log_fallback_prefix);
        memcpy(buffer_write_pointer, log_fallback_prefix, length);
        buffer_write_pointer += length;
        remaining_buffer_bytes -= length;
    }

    // add level
    const char *level_part = log_level_name_message_part(level);
    size_t level_part_length = strlen(level_part);
    memcpy(buffer_write_pointer, level_part, level_part_length);
    buffer_write_pointer += level_part_length;
    remaining_buffer_bytes -= level_part_length;

    // add the actual log message
    int res = vsnprintf(buffer_write_pointer, remaining_buffer_bytes, format, args);
    if (res < 0) {
        return false;
    } else {
        buffer_write_pointer += res;
        // remaining bytes don't need to be decremented as only termination follows
        // which has already been reserved
    }

    // terminate message
    memcpy(buffer_write_pointer, LINE_END, LINE_END_LENGTH);
    buffer_write_pointer += LINE_END_LENGTH;
    *buffer_write_pointer = 0;

    return true;
}

void xpmover_log(xpmover_log_level_t level, const char *format, ...) {
    if (!format) {
        return;
    }

    // skip if log level is disabled
    if (level < min_log_level) {
        return;
    }

    char buffer[MVLOG_MESSAGE_BUFFER_SIZE] = { 0 };

    va_list args;
    va_start(args, format);
    bool success = format_message(&buffer[0], level, (char*) format, args);
    va_end(args);
    
    if (!success) {
        printf(MVLOG_PREFIX "failed to format log message, format: \"%s\"" LINE_END, format);
        return;
    }

    char *message = &buffer[0];
    
    if (min_log_level_console <= level) {
        size_t length = strlen(message);

        // mutex is used to synchronize console output from within this project, if available
        // in case it is unavailable we should log anyway
        bool has_lock = false;
        if (has_log_mutex) {
            if (mtx_lock(&log_mutex) == thrd_success) {
                has_lock = true;
            } else {
                printf(MVLOG_PREFIX "failed to lock logger mutex" LINE_END);
            }
        }
        
        fwrite(message, sizeof(char), length, stdout);

        if (has_lock) {
            mtx_unlock(&log_mutex);
        }
    }

#ifdef HAVE_XPLM
    if (min_log_level_xplane <= level) {
        XPLMDebugString(message);
    }
#endif
}
