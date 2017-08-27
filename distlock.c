/**
 * Username: lusen
 * Description: distlock.c
 * CreateTime @2017-08-30 14:04:55
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#include "distlock.h"

enum mutex_status_s {
    MUTEX_STATUS_FREE,
    MUTEX_STATUS_LOCKED,
    MUTEX_STATUS_UNKNOWN,
};

struct distmutex_s {
    redisContext **ctx;
    size_t count;

    FILE *randfp;
    size_t quorum;
    size_t lockcount;

    char *vbuffer;
    uint32_t vallen: 8;
    uint32_t status: 2;
    uint32_t remain: 22;
};

distmutex_t *dist_mutex_init(const redisContext **ctx, size_t count, size_t vallen)
{
    distmutex_t *mutex = (distmutex_t *) malloc(sizeof(distmutex_t));
    if (!mutex) {
        return NULL;
    }

    mutex->ctx = (redisContext **) ctx;
    mutex->count = count;

    if (vallen > 64) {
        vallen = 64;
    }

    mutex->vallen = vallen;
    size_t length = 16;
    while (length <= vallen) {
        length <<= 1;
    }
    vallen = length;

    mutex->vbuffer = (char *) calloc(length, sizeof(char));
    if (mutex->vbuffer == NULL) {
        free(mutex);
        return NULL;
    }

    if (count > 1) {
        mutex->quorum = (count >> 1) + 1;
    } else {
        mutex->quorum = count;
    }
    mutex->lockcount = 0;

    mutex->randfp = fopen("/dev/urandom", "rb");
    if (mutex->randfp == NULL) {
        srandom(time(NULL));
    }

    mutex->status = MUTEX_STATUS_FREE;

    return mutex;
}

static inline int64_t __get_current_timems()
{
    struct timeval tv;
    int64_t timems = 0;
    if (gettimeofday(&tv, NULL) != 0) {
        return -1;
    }
    timems = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    return timems;
}

static inline void __get_random_value(distmutex_t *mutex)
{
    size_t i = 0;

    if (mutex->randfp) {
        unsigned char buffer[128];
        size_t length = mutex->vallen >> 1;
        fread(buffer, length, 1, mutex->randfp);
        if (!ferror(mutex->randfp)) {
            for (i = 0; i < length; i++) {
                snprintf(mutex->vbuffer + (i << 1), 3, "%02x", buffer[i]);
            }
            return;
        }
        clearerr(mutex->randfp);
    }

    static char *gbasevalue = (char *) "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    static int gbaselength = 0;

    if (gbaselength == 0) {
        gbaselength = strlen(gbasevalue);
    }

    int index = 0;
    for (i = 0; i < mutex->vallen; i++) {
        index = random() % gbaselength;
        mutex->vbuffer[i] = gbasevalue[index];
    }
}

static inline int __check_params(distmutex_t *mutex, const char *key, int status)
{
    if (!mutex) {
        return -ERR_MUTEX_NULL;
    }

    if (!key) {
        return -ERR_KEY_NULL;
    }

    if (mutex->status != status) {
        if (status == MUTEX_STATUS_FREE) {
            return -ERR_MUTEX_LOCKED;
        } else if (status == MUTEX_STATUS_LOCKED) {
            return -ERR_MUTEX_FREE;
        }
    }

    return MUTEX_OK;
}

int dist_mutex_lock(distmutex_t *mutex, const char *key, int expiretime)
{
    int iret = MUTEX_OK;
    if ( (iret = __check_params(mutex, key, MUTEX_STATUS_FREE)) != MUTEX_OK) {
        return iret;
    }

    if (expiretime <= 0) {
        expiretime = 60000;
    }

    // 1. It gets the current time in milliseconds.
    int64_t starttime = __get_current_timems();
    if (starttime < 0) {
        return -ERR_GETTIME_FAILED;
    }
    size_t i = 0;
    char command[1024];

    // 获取随机的value值
    __get_random_value(mutex);

    int length = snprintf(command, sizeof(command), "set %s %s nx px %d",
            key, mutex->vbuffer, expiretime);
    command[length] = '\0';

    mutex->lockcount = 0;

    /*
     * 2. It tries to acquire the lock in all the N instances sequentially, using
     * the same key name and random value in all the instances. During step 2,
     * when setting the lock in each instance, the client uses a timeout which
     * is small compared to the total lock auto-release time in order to acquire
     * it. For example if the auto-release time is 10 seconds, the timeout could
     * be in the ~ 5-50 milliseconds range. This prevents the client from remaining
     * blocked for a long time trying to talk with a Redis node which is down: if
     * an instance is not available, we should try to talk with the next instance ASAP.
     */
    for (i = 0; i < mutex->count; i++) {
        // 获取redis锁
        redisReply *reply = (redisReply *) redisCommand(mutex->ctx[i], command);
        if (reply) {
            if (!(reply->type == REDIS_REPLY_STATUS && strcasecmp(reply->str, "OK") == 0)) {
                freeReplyObject(reply);
                continue;
            }
            freeReplyObject(reply);
            mutex->lockcount++;
        } else {
            perror("reply is nil");
            break;
        }
    }

    /*
     * 3. The client computes how much time elapsed in order to acquire the lock,
     * by subtracting from the current time the timestamp obtained in step 1.
     * If and only if the client was able to acquire the lock in the majority of
     * the instances (at least 3), and the total time elapsed to acquire the lock
     * is less than lock validity time, the lock is considered to be acquired.
     */
    int64_t currtime = __get_current_timems();

    /*
     * 4. If the lock was acquired, its validity time is considered to be the initial
     * validity time minus the time elapsed, as computed in step 3.
     */
    int64_t expiretmp = expiretime - (currtime - starttime);
    if (expiretmp <= 0 || currtime < starttime || mutex->lockcount < mutex->quorum) {
        /*
         * 5. If the client failed to acquire the lock for some reason (either it was not
         * able to lock N/2+1 instances or the validity time is negative), it will try
         * to unlock all the instances (even the instances it believed it was not able to lock).
         */
        dist_mutex_unlock(mutex, key);
        if (expiretmp <= 0 || currtime < starttime) {
            return -ERR_LOCK_TIMEOUT;
        } else {
            return -ERR_QUORUM_FAILED;
        }
    }

#if 0
    if (expiretime > 10000 && (timeout - expiretime) > 1000) {
        length = snprintf(command, sizeof(command), "if redis.call('get', '%s') == '%s' then "
                "return redis.call('set', '%s', '%s', 'xx', 'px', '%ld') else return 0 end",
                key, mutex->vbuffer, key, mutex->vbuffer, expiretmp);
        command[length] = '\0';
        //static char *chgexpire_script = (char *) "if redis.call('get', KEYS[1]) == ARGV[1] then "
        //        "return redis.call('set', KEYS[1], ARGV[1], 'xx', 'px', ARGV[2]) else return 0 end";

        for (i = 0; i < mutex->count; i++) {
            redisReply *reply = (redisReply *) redisCommand(mutex->ctx[i], "eval %s 0", command);
            freeReplyObject(reply);
        }
    }
#endif

    mutex->status = MUTEX_STATUS_LOCKED;

    return iret;
}

int dist_mutex_trylock(distmutex_t *mutex, const char *key, int expiretime, int retries)
{
    int iret = MUTEX_OK;
    if ( (iret == __check_params(mutex, key, MUTEX_STATUS_FREE)) != MUTEX_OK) {
        return iret;
    }

    if (retries <= 0) {
        retries = 1;
    }

    int i = 0;

    for (i = 0; i < retries; i++) {
        if (dist_mutex_lock(mutex, key, expiretime) == MUTEX_OK) {
            return MUTEX_OK;
        }
    }

    return -ERR_LOCK_FAILED;
}

int dist_mutex_unlock(distmutex_t *mutex, const char *key)
{
    int iret = MUTEX_OK;
    if ( (iret = __check_params(mutex, key, MUTEX_STATUS_LOCKED)) != MUTEX_OK) {
        return iret;
    }

    size_t i = 0;
    static char *del_script = (char *) "if redis.call('get', KEYS[1]) == ARGV[1] then "
            "return redis.call('del', KEYS[1]) else return 0 end";
    static size_t dellen = 0;
    if (dellen == 0) {
        dellen = strlen(del_script);
    }

    const char *cmdargs[] = {
        (char *) "eval",
        del_script,
        (char *) "1",
        (char *) key,
        mutex->vbuffer,
    };
    const size_t lengths[] = {4, dellen, 1, strlen(key), mutex->vallen,};

    for (i = 0; i < mutex->count; i++) {
        redisReply *reply = (redisReply *) redisCommandArgv(mutex->ctx[i], 5, cmdargs, lengths);
        if (reply) {
            if (!(reply->type == REDIS_REPLY_INTEGER && reply->integer == 1)) {
                printf("[%s: %d] type = %d, str = %s\n", __FILE__, __LINE__, reply->type, reply->str);
                freeReplyObject(reply);

                // 判断key是否存在，不存在的话（已超时删除），则记为正常
                reply = (redisReply *) redisCommand(mutex->ctx[i], "get %s", key);
                if (!(reply->type == REDIS_REPLY_NIL)) {
                    // key仍存在，但是删除失败，可能不是自己获取的锁
                    continue;
                }
            }
            freeReplyObject(reply);
            mutex->lockcount--;
        } else {
            perror("reply is nil");
            break;
        }
    }

    if (mutex->lockcount > 0 && (mutex->lockcount > mutex->count - mutex->quorum)) {
        return -ERR_QUORUM_FAILED;
    }

    mutex->status = MUTEX_STATUS_FREE;

    return iret;
}

void dist_mutex_destroy(distmutex_t *mutex)
{
    if (mutex != NULL) {
        if (mutex->vbuffer) {
            free(mutex->vbuffer);
            mutex->vbuffer = NULL;
            mutex->vallen = 0;
        }

        if (mutex->randfp) {
            fclose(mutex->randfp);
            mutex->randfp = NULL;
        }

        free(mutex);
    }
}

int dist_mutex_status(distmutex_t *mutex)
{
    if (!mutex) {
        return -MUTEX_STATUS_UNKNOWN;
    }

    return mutex->status;
}


