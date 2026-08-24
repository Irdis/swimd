#ifndef SWIMD_LOG_H
#define SWIMD_LOG_H

typedef enum {
    SWIMD_INFO,
    SWIMD_WARN,
    SWIMD_ERR,
    SWIMD_DEBUG,
} SwimdLogLevel;

void swimd_log_init(const char *log_path);
void swimd_log_append(SwimdLogLevel level, const char *msg, ...);
void swimd_log_free(void);

#endif // SWIMD_LOG_H
