#ifndef ASIC_TASK_COMMON_H_
#define ASIC_TASK_COMMON_H_
#include "common.h"
#include "bm_job.h"
#include "mining.h"
void process_asic_result(task_result * asic_result, 
    bm_job * active_job, 
    uint8_t job_id,
    void (*SYSTEM_notify_found_nonce_callback)(double found_diff, uint32_t target),
    int (*stratum_submit_share_callback)(char * jobid, char * extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version));

bm_job * generate_work(mining_notify * notification, uint32_t extranonce_2, uint32_t difficulty);
void free_mining_notify(mining_notify * params);
#endif