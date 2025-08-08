#ifndef STRATUM_TASK_H_
#define STRATUM_TASK_H_

#include "mining.h"

void stratum_task(void * pvParameters);
mining_notify *get_mining_notification_from_stratum();
int stratum_submit_share(char * jobid, char * extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version);

#endif // STRATUM_TASK_H_