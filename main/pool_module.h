#ifndef POOL_MODULE_H_
#define POOL_MODULE_H_

typedef struct
{
    // The URL of the mining pool.
    char * url;

    // The port number on which the mining pool operates.
    uint16_t port;

    // Username for authenticating with the mining pool.
    char * user;

    // Password for authenticating with the mining pool.
    char * pass;

    // Difficulty level set on the mining pool.
    uint16_t difficulty;

    // Flag indicating whether this pool supports extranonce subscription.
    bool extranonce_subscribe;
    // Socket file descriptor used to communicate with the pool.
    int sock;
} PoolInfo;

#define POOL_MAIN 0
#define POOL_FALLBACK 1

typedef struct
{
    PoolInfo pools[2];

    // The average response time of the current (main or fallback) pool to requests.
    double response_time;

    // A flag indicating if the system is currently using the fallback pool instead of the main one.
    int active_pool;
} PoolModule;

extern PoolModule POOL_MODULE;
#endif