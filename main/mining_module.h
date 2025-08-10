#ifndef MINING_MODULE_H_
#define MINING_MODULE_H_

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "work_queue.h"
//seems to be unused. get set from Kconfig
#define STRATUM_USER CONFIG_STRATUM_USER
#define FALLBACK_STRATUM_USER CONFIG_FALLBACK_STRATUM_USER

/**
 * A structure containing various mining-related queues and parameters.
 */
typedef struct{

    /**
     * String to hold extra nonces used in mining.
     */
    char * extranonce_str;
    /**
     * Length of the second extra nonce.
     */
    int extranonce_2_len;

    /**
     * Indicates if a new set mining difficulty message is available.
     */
    bool new_set_mining_difficulty_msg;
    /**
     * Version mask for stratum protocol handling.
     */
    uint32_t version_mask;
    /**
     * Indicates if there's a new stratum version rolling message.
     */
    bool new_stratum_version_rolling_msg;

}mining_queues;

extern mining_queues MINING_MODULE;
#endif