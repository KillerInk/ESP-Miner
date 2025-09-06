#ifndef ASIC_TASK_H_
#define ASIC_TASK_H_

#include "stratum_api.h"

void ASIC_task(void *pvParameters);

void create_jobs_task(void *pvParameters);
void set_new_mining_notification(mining_notify * note);
void asic_task_init();
void ASIC_result_task(void *pvParameters);
extern int (*stratum_submit_share_callback)(char * jobid, char * extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version);

#endif /* ASIC_TASK_H_ */
