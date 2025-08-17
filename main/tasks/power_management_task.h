#ifndef POWER_MANAGEMENT_TASK_H_
#define POWER_MANAGEMENT_TASK_H_
#include "power_management_module.h"

#define POLL_RATE 500

void POWER_MANAGEMENT_init_frequency();

void POWER_MANAGEMENT_task(void * pvParameters);



#endif
