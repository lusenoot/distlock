# distlock for c implemented by redis

------
Based on redis [redlock](https://redis.io/topics/distlock)


## To create an lock manager
```c
API: distmutex_t *dist_mutex_init(const redisContext **ctx, size_t count, size_t vallen);

distmutex_t *mutex = dist_mutex_init(redisctx, ctx_count, 16);
```

## Acquire a lock
```c
API:
    int dist_mutex_lock(distmutex_t *mutex, const char *key, int expiretime);
    int dist_mutex_trylock(distmutex_t *mutex, const char *key, int expiretime, int retries);

int iret = dist_mutex_lock(mutex, "my_key_name", 10000);
if (iret != MUTEX_OK) {
    printf("lock failed\n");
}

Or:
int iret = dist_mutex_trylock(mutex, "my_key_name", 10000, 3);
if (iret != MUTEX_OK) {
    printf("lock failed\n");
}
```

## Release a lock
```c
API: int dist_mutex_unlock(distmutex_t *mutex, const char *key);

int iret = dist_mutex_unlock(mutex, "my_key_name");
if (iret != MUTEX_OK) {
    printf("lock failed\n");
}
```

## Get lock status
```c
API: int dist_mutex_status(distmutex_t *mutex);

status: 0 for free, 1 for busy; but if mutex is NULL, return -2.
int status = dist_mutex_status(mutex);
```

## Release lock manager
```c
API: void dist_mutex_destroy(distmutex_t *mutex);

dist_mutex_destroy(mutex);
```


