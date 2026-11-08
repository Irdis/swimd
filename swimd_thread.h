#ifndef SWIMD_THREAD_H
#define SWIMD_THREAD_H

#ifdef _WIN32
    #include <windows.h>
    #include <stdbool.h>
#else
    #include <pthread.h>
    #include <dirent.h>
    #include <time.h>
#endif

#ifdef _WIN32

#define SwimdThread HANDLE
#define SwimdCritSection CRITICAL_SECTION
#define SwimdAutoResetEvent HANDLE
#define SwimdManualResetEvent HANDLE
#define SwimdThreadCallbackResult DWORD
#define SwimdThreadCallbackSpecifier WINAPI
#define SwimdThreadCallbackArg LPVOID

#else

#define SwimdThread pthread_t
#define SwimdCritSection pthread_mutex_t
#define SwimdThreadCallbackResult void*
#define SwimdThreadCallbackArg void*
#define SwimdThreadCallbackSpecifier

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool signaled;
} SwimdAutoResetEvent;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool signaled;
} SwimdManualResetEvent;

#endif // _WIN32

typedef SwimdThreadCallbackResult SwimdThreadCallbackSpecifier (*swimd_thread_callback)(SwimdThreadCallbackArg);
void swimd_thread_create(SwimdThread *t,
        swimd_thread_callback callback,
        void *arg);
void swimd_thread_join(SwimdThread *t);
void swimd_thread_close(SwimdThread *t);

void swimd_crit_init(SwimdCritSection *lock);
void swimd_crit_lock(SwimdCritSection *lock);
void swimd_crit_unlock(SwimdCritSection *lock);
void swimd_crit_close(SwimdCritSection *lock);

void swimd_are_init(SwimdAutoResetEvent *ev, bool initial_state);
void swimd_are_wait(SwimdAutoResetEvent *ev);
void swimd_are_set(SwimdAutoResetEvent *ev);
void swimd_are_close(SwimdAutoResetEvent *ev);

void swimd_mre_init(SwimdManualResetEvent *ev, bool initial_state);
void swimd_mre_wait(SwimdManualResetEvent *ev);
void swimd_mre_set(SwimdManualResetEvent *ev);
void swimd_mre_reset(SwimdManualResetEvent *ev);
void swimd_mre_close(SwimdManualResetEvent *ev);

void swimd_thread_sleep(int milliseconds);

#endif // SWIMD_THREAD_H
