/*
 * Minimal FreeSWITCH API stubs for compile-checking mod_realtime_ws without FS headers.
 */
#ifndef RTW_FS_STUB_SWITCH_H
#define RTW_FS_STUB_SWITCH_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum { SWITCH_STATUS_SUCCESS = 0, SWITCH_STATUS_FALSE = 1 } switch_status_t;
typedef struct switch_memory_pool switch_memory_pool_t;
typedef struct switch_loadable_module_interface switch_loadable_module_interface_t;
typedef struct switch_api_interface switch_api_interface_t;
typedef struct switch_core_session switch_core_session_t;

typedef struct switch_stream_handle {
    int (*write_function)(struct switch_stream_handle *, const char *fmt, ...);
} switch_stream_handle_t;

#define SWITCH_CHANNEL_LOG NULL
#define SWITCH_LOG_NOTICE 0

static inline void switch_log_printf(void *ch, int level, const char *fmt, ...)
{
    (void)ch;
    (void)level;
    (void)fmt;
}

#define zstr(s) (!(s) || !*(s))

static inline int switch_separate_string(char *buf, char delim, char **array, int arraylen)
{
    int argc = 0;
    char *p = buf;
    if (!buf) {
        return 0;
    }
    while (argc < arraylen) {
        array[argc++] = p;
        p = strchr(p, delim);
        if (!p) {
            break;
        }
        *p++ = '\0';
    }
    return argc;
}

#define SWITCH_MODULE_LOAD_FUNCTION(name) \
    switch_status_t name(switch_loadable_module_interface_t **module_interface, switch_memory_pool_t *pool)

#define SWITCH_MODULE_SHUTDOWN_FUNCTION(name) switch_status_t name(void)

#define SWITCH_MODULE_DEFINITION(name, load, shutdown, runtime) \
    static const char *modname = #name

#define SWITCH_STANDARD_API(name)                                                                              \
    static int __attribute__((unused)) name##_stream_write(switch_stream_handle_t *s, const char *fmt, ...)   \
    {                                                                                                          \
        (void)s;                                                                                               \
        (void)fmt;                                                                                             \
        return 0;                                                                                              \
    }                                                                                                          \
    switch_status_t name(const char *cmd, switch_core_session_t *session, switch_stream_handle_t *stream)

static inline switch_loadable_module_interface_t *
switch_loadable_module_create_module_interface(switch_memory_pool_t *pool, const char *name)
{
    (void)pool;
    (void)name;
    return (switch_loadable_module_interface_t *)(uintptr_t)1;
}

#define SWITCH_ADD_API(interface, name, desc, func, syntax) \
    do {                                                    \
        (void)(interface);                                  \
        (void)(name);                                       \
        (void)(desc);                                       \
        (void)(func);                                       \
        (void)(syntax);                                     \
    } while (0)

#endif
