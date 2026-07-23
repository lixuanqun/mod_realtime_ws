/*
 * Expanded FreeSWITCH stubs for compiling/testing mod_realtime_ws without libfreeswitch-dev.
 * Enough to exercise media-bug style callbacks via the harness.
 */
#ifndef RTW_FS_STUB_SWITCH_H
#define RTW_FS_STUB_SWITCH_H

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum {
    SWITCH_STATUS_SUCCESS = 0,
    SWITCH_STATUS_FALSE = 1,
    SWITCH_STATUS_TERM = 2
} switch_status_t;

typedef enum { SWITCH_FALSE = 0, SWITCH_TRUE = 1 } switch_bool_t;

typedef enum {
    SWITCH_ABC_TYPE_INIT = 0,
    SWITCH_ABC_TYPE_CLOSE,
    SWITCH_ABC_TYPE_READ,
    SWITCH_ABC_TYPE_WRITE,
    SWITCH_ABC_TYPE_WRITE_REPLACE,
    SWITCH_ABC_TYPE_READ_REPLACE
} switch_abc_type_t;

typedef uint32_t switch_media_bug_flag_t;
#define SMBF_READ_STREAM (1 << 0)
#define SMBF_WRITE_STREAM (1 << 1)
#define SMBF_WRITE_REPLACE (1 << 2)
#define SMBF_READ_REPLACE (1 << 3)
#define SMBF_STEREO (1 << 4)

typedef struct switch_memory_pool switch_memory_pool_t;
typedef struct switch_loadable_module_interface switch_loadable_module_interface_t;
typedef struct switch_api_interface switch_api_interface_t;
typedef struct switch_mutex switch_mutex_t;
typedef struct switch_media_bug switch_media_bug_t;
typedef struct switch_channel switch_channel_t;
typedef struct switch_codec switch_codec_t;

typedef struct switch_core_session {
    char uuid[64];
    switch_channel_t *channel;
    void *bug_private;
    switch_media_bug_t *bug;
} switch_core_session_t;

typedef struct switch_frame {
    void *data;
    uint32_t datalen;
    uint32_t buflen;
    uint32_t samples;
    uint32_t rate;
    uint32_t channels;
} switch_frame_t;

struct switch_channel {
    char name[128];
    void *privates[8];
    const char *private_keys[8];
    int pre_answered;
};

struct switch_media_bug {
    switch_core_session_t *session;
    void *user_data;
    switch_frame_t *write_replace_frame_in;
    switch_frame_t *write_replace_frame_out;
};

struct switch_mutex {
    pthread_mutex_t m;
};

typedef struct switch_stream_handle {
    int (*write_function)(struct switch_stream_handle *, const char *fmt, ...);
} switch_stream_handle_t;

#define SWITCH_CHANNEL_LOG NULL
#define SWITCH_LOG_DEBUG 7
#define SWITCH_LOG_INFO 6
#define SWITCH_LOG_NOTICE 5
#define SWITCH_LOG_WARNING 4
#define SWITCH_LOG_ERROR 3
#define SWITCH_CHANNEL_SESSION_LOG(s) (s)

static inline void switch_log_printf(void *ch, int level, const char *fmt, ...)
{
    (void)ch;
    (void)level;
    (void)fmt;
}

#define zstr(s) (!(s) || !*(s))
#define switch_safe_free(p) \
    do {                    \
        if (p) {            \
            free(p);        \
            (p) = NULL;     \
        }                   \
    } while (0)

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

#define SWITCH_MODULE_DEFINITION(name, load, shutdown, runtime) static const char *modname = #name

#define SWITCH_STANDARD_API(name)                                                                                    \
    static int __attribute__((unused)) name##_stream_write(switch_stream_handle_t *s, const char *fmt, ...)         \
    {                                                                                                                \
        (void)s;                                                                                                     \
        (void)fmt;                                                                                                   \
        return 0;                                                                                                    \
    }                                                                                                                \
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

static inline switch_status_t switch_mutex_init(switch_mutex_t **mutex, int flags, switch_memory_pool_t *pool)
{
    (void)flags;
    (void)pool;
    *mutex = (switch_mutex_t *)calloc(1, sizeof(switch_mutex_t));
    if (!*mutex) {
        return SWITCH_STATUS_FALSE;
    }
    pthread_mutex_init(&(*mutex)->m, NULL);
    return SWITCH_STATUS_SUCCESS;
}

static inline switch_status_t switch_mutex_lock(switch_mutex_t *mutex)
{
    return pthread_mutex_lock(&mutex->m) == 0 ? SWITCH_STATUS_SUCCESS : SWITCH_STATUS_FALSE;
}

static inline switch_status_t switch_mutex_trylock(switch_mutex_t *mutex)
{
    return pthread_mutex_trylock(&mutex->m) == 0 ? SWITCH_STATUS_SUCCESS : SWITCH_STATUS_FALSE;
}

static inline switch_status_t switch_mutex_unlock(switch_mutex_t *mutex)
{
    return pthread_mutex_unlock(&mutex->m) == 0 ? SWITCH_STATUS_SUCCESS : SWITCH_STATUS_FALSE;
}

static inline void switch_mutex_destroy(switch_mutex_t *mutex)
{
    if (!mutex) {
        return;
    }
    pthread_mutex_destroy(&mutex->m);
    free(mutex);
}

static inline switch_channel_t *switch_core_session_get_channel(switch_core_session_t *session)
{
    return session ? session->channel : NULL;
}

static inline const char *switch_core_session_get_uuid(switch_core_session_t *session)
{
    return session ? session->uuid : "";
}

static inline void *switch_channel_get_private(switch_channel_t *channel, const char *key)
{
    int i;
    if (!channel || !key) {
        return NULL;
    }
    for (i = 0; i < 8; i++) {
        if (channel->private_keys[i] && !strcmp(channel->private_keys[i], key)) {
            return channel->privates[i];
        }
    }
    return NULL;
}

static inline void switch_channel_set_private(switch_channel_t *channel, const char *key, void *val)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (!channel->private_keys[i] || !strcmp(channel->private_keys[i], key)) {
            channel->private_keys[i] = key;
            channel->privates[i] = val;
            return;
        }
    }
}

static inline switch_status_t switch_channel_pre_answer(switch_channel_t *channel)
{
    if (!channel) {
        return SWITCH_STATUS_FALSE;
    }
    channel->pre_answered = 1;
    return SWITCH_STATUS_SUCCESS;
}

static inline switch_core_session_t *switch_core_session_locate(const char *uuid)
{
    (void)uuid;
    return NULL; /* harness uses direct session pointers */
}

static inline void switch_core_session_rwunlock(switch_core_session_t *session)
{
    (void)session;
}

static inline switch_status_t switch_event_reserve_subclass(const char *name)
{
    (void)name;
    return SWITCH_STATUS_SUCCESS;
}

static inline switch_status_t switch_event_free_subclass(const char *name)
{
    (void)name;
    return SWITCH_STATUS_SUCCESS;
}

static inline void switch_console_set_complete(const char *s)
{
    (void)s;
}

/* Harness helpers */
static inline switch_core_session_t *rtw_stub_session_create(const char *uuid)
{
    switch_core_session_t *s = (switch_core_session_t *)calloc(1, sizeof(*s));
    switch_channel_t *ch = (switch_channel_t *)calloc(1, sizeof(*ch));
    if (!s || !ch) {
        free(s);
        free(ch);
        return NULL;
    }
    snprintf(s->uuid, sizeof(s->uuid), "%s", uuid ? uuid : "stub-uuid");
    snprintf(ch->name, sizeof(ch->name), "stub/%s", s->uuid);
    s->channel = ch;
    return s;
}

static inline void rtw_stub_session_destroy(switch_core_session_t *s)
{
    if (!s) {
        return;
    }
    free(s->channel);
    free(s);
}

static inline switch_frame_t *switch_core_media_bug_get_write_replace_frame(switch_media_bug_t *bug)
{
    return bug ? bug->write_replace_frame_in : NULL;
}

static inline void switch_core_media_bug_set_write_replace_frame(switch_media_bug_t *bug, switch_frame_t *frame)
{
    if (bug) {
        bug->write_replace_frame_out = frame;
    }
}

#endif
