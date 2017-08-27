/**
 * Username: lusen
 * Description: testlock.c
 * CreateTime @2017-08-30 14:14:44
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "distlock.h"

int main(int argc, char *argv[])
{
    int timeout = 60;
    int loopcnt = 100;

    if (argc > 1) {
        timeout = atoi(argv[1]);
        if (timeout <= 0 || timeout >= 120) {
            timeout = 60;
        }
    }

    if (argc > 2) {
        loopcnt = atoi(argv[2]);
        if (loopcnt <= 0) {
            loopcnt = 100;
        }
    }

    timeout *= 1000;

    redisContext *ctx = (redisContext *) redisConnect("127.0.0.1", 6379);
    //redisContext *ctx = (redisContext *) redisConnectNonBlock("127.0.0.1", 6379);
    char *basekey = (char *) "message";
    char key[128] = {0};
    int i = 0;

    distmutex_t *mutex = (distmutex_t *) dist_mutex_init((const redisContext **) &ctx, 1, 16);

    for (i = 0 ; i < loopcnt; i++) {
        snprintf(key, sizeof(key), "%s%d", basekey, i);

        int iret = dist_mutex_lock(mutex, key, timeout);
        if (iret != MUTEX_OK) {
            printf("dist_mutex_lock failed: %s\n", key);
        }
        //sleep(10);
        iret = dist_mutex_unlock(mutex, key);
        if (iret != MUTEX_OK) {
            printf("dist_mutex_unlock failed: %s\n", key);
        }
    }

    dist_mutex_destroy(mutex);

    redisFree(ctx);

    return 0;
}


