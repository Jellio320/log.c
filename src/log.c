/*
 * Copyright (c) 2020 rxi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "log.h"

#define MAX_CALLBACKS 32

typedef struct Callback {
    log_LogFn fn;
    void *udata;
    int level;
} Callback;

static struct {
    void *udata;
    log_LockFn lock;
    int level;
    bool quiet;
    Callback callbacks[MAX_CALLBACKS];
} L = {
    .udata = NULL,
    .lock = NULL,
    .level = LOGC_TRACE,
    .quiet = false,
    .callbacks = { NULL }
};

static const char *level_strings[6] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

#ifdef LOG_USE_COLOR
static const char *level_colors[6] = {
    "\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
};
#endif

#ifdef LOG_USE_PTHREAD_NAMES
#define PTHREAD_NAME_FMT "[%s]"
#define PTHREAD_NAME_VALUE thread,
#else
#define PTHREAD_NAME_FMT
#define PTHREAD_NAME_VALUE
#endif

const char *log_level_string(int level) {
    return level_strings[level];
}

void log_set_lock(log_LockFn fn, void *udata) {
    L.lock = fn;
    L.udata = udata;
}

static void stdout_callback(log_Event *ev) {
    char time[16];
    time[strftime(time, sizeof(time), "%H:%M:%S", ev->time)] = 0;
#ifdef LOG_USE_PTHREAD_NAMES
    char thread[16] = "";
    pthread_getname_np(ev->thread, thread, 16);
#endif

#ifdef LOG_USE_COLOR
    fprintf(ev->udata, "%s[%s][%-5s]" PTHREAD_NAME_FMT "[%s:%d]:\x1b[0m ", level_colors[ev->level], time, level_strings[ev->level], PTHREAD_NAME_VALUE ev->file, ev->line);
#else
    fprintf(ev->udata, "[%s][%-5s]" PTHREAD_NAME_FMT "[%s:%d]: ", time, level_strings[ev->level], PTHREAD_NAME_VALUE ev->file, ev->line);
#endif
    vfprintf(ev->udata, ev->fmt, ev->ap);
    fputc('\n', ev->udata);
    fflush(ev->udata);
}

void log_set_level(int level) {
    L.level = level;
}

void log_set_quiet(bool enable) {
    L.quiet = enable;
}

int log_get_max_callbacks(void) {
    return MAX_CALLBACKS;
}

int log_push_callback(log_LogFn fn, void *udata, int level) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if(L.callbacks[i].fn)
            continue;

            L.callbacks[i] = (Callback) { fn, udata, level };
        return i + 1;
    }
    return 0;
}

int log_pop_callback(void) {
    for(int i = MAX_CALLBACKS - 1; i >= 0; i--) {
        if(!L.callbacks[i].fn)
            continue;
        
        L.callbacks[i] = (Callback) { NULL, NULL, 0 };
        return i;
    }

    return 0;
}

int log_add_callback(log_LogFn fn, void *udata, int level) {
    return log_push_callback(fn, udata, level);
}

static void file_callback(log_Event *ev) {
    char time[64];
    time[strftime(time, sizeof(time), "%Y-%m-%d %H:%M:%S", ev->time)] = 0;
#ifdef LOG_USE_PTHREAD_NAMES
    char thread[16] = "";
    pthread_getname_np(ev->thread, thread, 16);
#endif
    fprintf(ev->udata, "[%s][%-5s]" PTHREAD_NAME_FMT "[%s:%d] ", time, level_strings[ev->level], PTHREAD_NAME_VALUE ev->file, ev->line);
    vfprintf(ev->udata, ev->fmt, ev->ap);
    fputc('\n', ev->udata);
    fflush(ev->udata);
}

int log_add_fp(FILE *fp, int level) {
    return log_push_callback(file_callback, fp, level);
}

void log_log(int level, const char *file, int line, const char *fmt, ...) {
    log_Event ev = {
        .fmt = (fmt != NULL ? fmt : "(null)"),
        .file = file,
        .line = line,
        .level = level,
    };

    if (L.lock) {
        L.lock(true, L.udata);
    }

    if (!L.quiet && level >= L.level) {
        time_t t = time(NULL);
        ev.time = localtime(&t);
        ev.thread = pthread_self();
        ev.udata = stderr;

        va_start(ev.ap, fmt);

        stdout_callback(&ev);

        va_end(ev.ap);
    }

    for (int i = 0; i < MAX_CALLBACKS; i++) {
        Callback *cb = &L.callbacks[i];
        if (cb->fn == NULL)
            break;

        if (level < cb->level)
            continue;

            if (!ev.time) {
                time_t t = time(NULL);
                ev.time = localtime(&t);
                ev.thread = pthread_self();
            }

            ev.udata = cb->udata;

            va_start(ev.ap, fmt);
            cb->fn(&ev);
            va_end(ev.ap);
        }
    }

    if (L.lock) {
        L.lock(false, L.udata);
    }
}
