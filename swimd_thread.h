#ifndef SWIMD_THREAD_H
#define SWIMD_THREAD_H

#ifdef _WIN32
    #include <windows.h>
#else
    #include <pthread.h>
    #include <dirent.h>
#endif
#include <stdbool.h>

#ifdef _WIN32

typedef DWORD (*swimd_thread_callback)(LPVOID);
void swimd_thread_create(HANDLE *t, swimd_thread_callback callback);
void swimd_thread_join(HANDLE *t);
void swimd_thread_close(HANDLE *t);

void swimd_crit_init(CRITICAL_SECTION *lock);
void swimd_crit_lock(CRITICAL_SECTION *lock);
void swimd_crit_unlock(CRITICAL_SECTION *lock);
void swimd_crit_close(CRITICAL_SECTION *lock);

void swimd_are_init(HANDLE *ev, bool initial_state);
void swimd_are_wait(HANDLE *ev);
void swimd_are_set(HANDLE *ev);
void swimd_are_close(HANDLE *ev);

void swimd_mre_init(HANDLE *ev, bool initial_state);
void swimd_mre_wait(HANDLE *ev);
void swimd_mre_set(HANDLE *ev);
void swimd_mre_reset(HANDLE *ev);
void swimd_mre_close(HANDLE *ev);

#else

typedef void* (*swimd_thread_callback)(void*);

void swimd_thread_create(pthread_t *t, swimd_thread_callback callback);
void swimd_thread_join(pthread_t *t);
void swimd_thread_close(pthread_t *t);

void swimd_crit_init(pthread_mutex_t *lock);
void swimd_crit_lock(pthread_mutex_t *lock);
void swimd_crit_unlock(pthread_mutex_t *lock);
void swimd_crit_close(pthread_mutex_t *lock);

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool signaled;
} SwimdAutoResetEvent;

void swimd_are_init(SwimdAutoResetEvent *ev, bool initial_state);
void swimd_are_wait(SwimdAutoResetEvent *ev);
void swimd_are_set(SwimdAutoResetEvent *ev);
void swimd_are_close(SwimdAutoResetEvent *ev);

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool signaled;
} SwimdManualResetEvent;

void swimd_mre_init(SwimdManualResetEvent *ev, bool initial_state);
void swimd_mre_wait(SwimdManualResetEvent *ev);
void swimd_mre_set(SwimdManualResetEvent *ev);
void swimd_mre_reset(SwimdManualResetEvent *ev);
void swimd_mre_close(SwimdManualResetEvent *ev);

#endif // _WIN32
#endif // SWIMD_THREAD_H
