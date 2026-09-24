#include "swimd_log.h"
#include "swimd_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>

static FILE *swimd_log = {0};
static bool swimd_log_enabled = false;

#ifdef _WIN32
static CRITICAL_SECTION scan_log_lock;
#else
static pthread_mutex_t scan_log_lock;
#endif


void swimd_log_init(const char *log_path) {
    if (log_path == NULL) {
        return;
    }
    swimd_log_enabled = true;
    swimd_log = fopen(log_path, "a");
    swimd_crit_init(&scan_log_lock);
    if (swimd_log == NULL) {
        fprintf(stderr, "Unable to init log file");
        exit(1);
    }
}

void swimd_log_append(SwimdLogLevel level, const char *msg, ...) {
    if (!swimd_log_enabled) {
        return;
    }

    swimd_crit_lock(&scan_log_lock);
    const char *level_str;
    switch (level) {
        case SWIMD_INFO:
            level_str = "INFO";
            break;
        case SWIMD_WARN:
            level_str = "WARN";
            break;
        case SWIMD_ERR:
            level_str = "ERR";
            break;
        case SWIMD_DEBUG:
            level_str = "DEBUG";
            break;
    }
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    time_t seconds = ts.tv_sec;

    struct tm t;
#ifdef _WIN32
    localtime_s(&t, &seconds);
#else
    localtime_r(&seconds, &t);
#endif

#ifdef DEBUG_PRINT
    printf("%04d-%02d-%02dT%02d:%02d:%02d.%09ld ",
        t.tm_year+1900,
        t.tm_mon+1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec,
        ts.tv_nsec
    );
    printf("[%s] ", level_str);
#endif

    fprintf(swimd_log, "%04d-%02d-%02dT%02d:%02d:%02d.%09ld ",
        t.tm_year+1900,
        t.tm_mon+1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec,
        ts.tv_nsec
    );
    fprintf(swimd_log, "[%s] ", level_str);

    va_list args;
    va_start(args, msg);

#ifdef DEBUG_PRINT
    va_list args_copy;
    va_copy(args_copy, args);
    vprintf(msg, args_copy);
    va_end(args_copy);
#endif
    vfprintf(swimd_log, msg, args);
    va_end(args);

#ifdef DEBUG_PRINT
    printf("\n");
#endif
    fprintf(swimd_log, "\n");
    fflush(swimd_log);
    swimd_crit_unlock(&scan_log_lock);
}

void swimd_log_free(void) {
    if (!swimd_log_enabled) {
        return;
    }
    swimd_log_enabled = false;
    swimd_crit_close(&scan_log_lock);
    fclose(swimd_log);
}
