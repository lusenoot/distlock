/**
 * Username: lusen
 * Description: distlock.h
 * CreateTime @2017-08-30 14:04:47
 */

#ifndef __DISTLOCK_H_H_H__
#define __DISTLOCK_H_H_H__

#include <hiredis/hiredis.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum distmutex_errcode_s {
    MUTEX_OK,
    ERR_MUTEX_NULL,
    ERR_KEY_NULL,
    ERR_MUTEX_LOCKED,
    ERR_MUTEX_FREE,
    ERR_GETTIME_FAILED,
    ERR_LOCK_TIMEOUT,
    ERR_QUORUM_FAILED,
    ERR_LOCK_FAILED,
};

typedef struct distmutex_s distmutex_t;

distmutex_t *dist_mutex_init(const redisContext **ctx, size_t count, size_t vallen);
void dist_mutex_destroy(distmutex_t *mutex);
int dist_mutex_lock(distmutex_t *mutex, const char *key, int timeout);
int dist_mutex_trylock(distmutex_t *mutex, const char *key, int timeout, int retries);
int dist_mutex_unlock(distmutex_t *mutex, const char *key);
int dist_mutex_status(distmutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif // end of __DISTLOCK_H_H_H__


