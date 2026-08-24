#include "swimd_thread.h"

#ifdef _WIN32

void swimd_thread_create(HANDLE *t,
        swimd_thread_callback callback,
        void *arg) {
    *t = CreateThread(NULL,
            0,
            callback,
            (LPVOID)arg,
            0,
            NULL);
}

void swimd_thread_join(HANDLE *t) {
    WaitForSingleObject(*t, INFINITE);
}

void swimd_thread_close(HANDLE *t) {
    CloseHandle(*t);
}

void swimd_crit_init(CRITICAL_SECTION *lock) {
    InitializeCriticalSection(lock);
}

void swimd_crit_lock(CRITICAL_SECTION *lock) {
    EnterCriticalSection(lock);
}

void swimd_crit_unlock(CRITICAL_SECTION *lock) {
    LeaveCriticalSection(lock);
}

void swimd_crit_close(CRITICAL_SECTION *lock) {
    DeleteCriticalSection(lock);
}

void swimd_are_init(HANDLE *ev, bool initial_state) {
    *ev = CreateEvent(NULL, false, initial_state, NULL);
}

void swimd_are_wait(HANDLE *ev) {
    WaitForSingleObject(*ev, INFINITE);
}

void swimd_are_set(HANDLE *ev) {
    SetEvent(*ev);
}

void swimd_are_close(HANDLE *ev) {
    CloseHandle(*ev);
}

void swimd_mre_init(HANDLE *ev, bool initial_state) {
    *ev = CreateEvent(NULL, true, initial_state, NULL);
}

void swimd_mre_wait(HANDLE *ev) {
    WaitForSingleObject(*ev, INFINITE);
}

void swimd_mre_set(HANDLE *ev) {
    SetEvent(*ev);
}

void swimd_mre_reset(HANDLE *ev) {
    ResetEvent(*ev);
}

void swimd_mre_close(HANDLE *ev) {
    CloseHandle(*ev);
}
#else
typedef void* (*swimd_thread_callback)(void*);

void swimd_thread_create(pthread_t *t,
        swimd_thread_callback callback,
        void *arg) {
    pthread_create(t, NULL, callback, arg);
}

void swimd_thread_join(pthread_t *t) {
   pthread_join(*t, NULL);
}

void swimd_thread_close(pthread_t *t) {
    (void)t;
}

void swimd_crit_init(pthread_mutex_t *lock) {
    pthread_mutex_init(lock, NULL);
}

void swimd_crit_lock(pthread_mutex_t *lock) {
    pthread_mutex_lock(lock);
}

void swimd_crit_unlock(pthread_mutex_t *lock) {
    pthread_mutex_unlock(lock);
}

void swimd_crit_close(pthread_mutex_t *lock) {
    pthread_mutex_destroy(lock);
}

void swimd_are_init(SwimdAutoResetEvent *ev, bool initial_state) {
    pthread_mutex_init(&ev->mutex, NULL);
    pthread_cond_init(&ev->condition, NULL);
    ev->signaled = initial_state;
}

void swimd_are_wait(SwimdAutoResetEvent *ev) {
    pthread_mutex_lock(&ev->mutex);
    while (!ev->signaled) {
        pthread_cond_wait(&ev->condition, &ev->mutex);
    }
    ev->signaled = false;
    pthread_mutex_unlock(&ev->mutex);
}

void swimd_are_set(SwimdAutoResetEvent *ev) {
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = true;
    pthread_mutex_unlock(&ev->mutex);
    pthread_cond_signal(&ev->condition);
}

void swimd_are_close(SwimdAutoResetEvent *ev) {
    pthread_mutex_destroy(&ev->mutex);
    pthread_cond_destroy(&ev->condition);
}

void swimd_mre_init(SwimdManualResetEvent *ev, bool initial_state) {
    pthread_mutex_init(&ev->mutex, NULL);
    pthread_cond_init(&ev->condition, NULL);
    ev->signaled = initial_state;
}

void swimd_mre_wait(SwimdManualResetEvent *ev) {
    pthread_mutex_lock(&ev->mutex);
    while (!ev->signaled) {
        pthread_cond_wait(&ev->condition, &ev->mutex);
    }
    pthread_mutex_unlock(&ev->mutex);
}

void swimd_mre_set(SwimdManualResetEvent *ev) {
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = true;
    pthread_cond_broadcast(&ev->condition);
    pthread_mutex_unlock(&ev->mutex);
}

void swimd_mre_reset(SwimdManualResetEvent *ev) {
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = false;
    pthread_mutex_unlock(&ev->mutex);
}

void swimd_mre_close(SwimdManualResetEvent *ev) {
    pthread_mutex_destroy(&ev->mutex);
    pthread_cond_destroy(&ev->condition);
}
#endif
