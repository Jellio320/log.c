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

typedef struct {
    log_LogFn fn;
    void* udata;
    int level;
} Callback;

static struct {
    void* udata;
    log_LockFn lock;
    int level;
    bool quiet;
    Callback callbacks[MAX_CALLBACKS];
} L;

static const char level_strings[6][6] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

#ifdef LOG_USE_COLOR
static const char level_colors[6][6] = {
    "\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
};
#endif


const char* log_level_string(int level) {
    return level_strings[level];
}

void log_set_lock(log_LockFn fn, void* udata) {
    L.lock = fn;
    L.udata = udata;
}

void log_set_level(int level) {
    L.level = level;
}

void log_set_quiet(bool enable) {
    L.quiet = enable;
}

int log_add_callback(log_LogFn fn, void* udata, int level) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!L.callbacks[i].fn) {
            L.callbacks[i] = (Callback) { fn, udata, level };
            return 1;
        }
    }
    return 0;
}

static void file_callback(log_Event* ev) {
    char time[64], thread[32] = { 0 };
    time[strftime(time, sizeof(time), "%Y-%m-%d %H:%M:%S", ev->time)] = 0;
    pthread_getname_np(pthread_self(), thread, sizeof(thread));
    fprintf(ev->udata, "[%s][%-5s][%s][%s:%d] ", time, level_strings[ev->level], thread, ev->file, ev->line);
    vfprintf(ev->udata, ev->fmt, ev->ap);
    fprintf(ev->udata, "\n");
    fflush(ev->udata);
}

int log_add_fp(FILE* fp, int level) {
    return log_add_callback(file_callback, fp, level);
}

void log_log(int level, const char* file, int line, const char* fmt, ...) {
    log_Event ev = {
        .fmt = (fmt != NULL ? fmt : "(null)"),
        .file = file,
        .line = line,
        .level = level,
    };

    // <lock>
    if (L.lock) {
        L.lock(true, L.udata);
    }
    // </lock>

    if (!L.quiet && level >= L.level) {
        // <init_event>
        time_t t = time(NULL);
        ev.time = localtime(&t);
        ev.udata = stderr;
        // </init_event>

        va_start(ev.ap, fmt);

        // <stdout_callback>
        char time[16], thread[16] = { 0 };
        time[strftime(time, sizeof(time), "%H:%M:%S", ev.time)] = 0;
        pthread_getname_np(pthread_self(), thread, sizeof(thread));
#ifdef LOG_USE_COLOR
        fprintf(ev.udata, "%s[%s][%-5s][%s][%s:%d]:\x1b[0m ", level_colors[ev.level], time, level_strings[ev.level], thread, ev.file, ev.line);
#else
        fprintf(ev.udata, "[%s][%-5s][%s][%s:%d]: ", time, level_strings[ev.level], thread, ev.file, ev.line);
#endif
        vfprintf(ev.udata, ev.fmt, ev.ap);
        fprintf(ev.udata, "\n");
        fflush(ev.udata);
        // </stdout_callback>

        va_end(ev.ap);
    }

    for (Callback *cb = L.callbacks, *end = &L.callbacks[MAX_CALLBACKS]; cb != end && cb->fn; cb++) {
        if (level >= cb->level) {
            // <init_event>
            if (!ev.time) {
                time_t t = time(NULL);
                ev.time = localtime(&t);
            }
            ev.udata = cb->udata;
            // </init_event>

            va_start(ev.ap, fmt);
            cb->fn(&ev);
            va_end(ev.ap);
        }
    }

    // <unlock>
    if (L.lock) {
        L.lock(false, L.udata);
    }
    // </unlock>
}
