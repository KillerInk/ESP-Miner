#ifndef ASIC_TASK_H_
#define ASIC_TASK_H_

#include "stratum_api.h"

void ASIC_task(void *pvParameters);

void create_jobs_task(void *pvParameters);
void set_new_mining_notification(mining_notify * note);
void asic_task_init();

#endif /* ASIC_TASK_H_ */
