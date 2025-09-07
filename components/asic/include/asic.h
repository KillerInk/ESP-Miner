#ifndef ASIC_H
#define ASIC_H

#include <esp_err.h>
#include "common.h"
#include "stdbool.h"
#include "stratum_api.h"
#include "mining.h"

void ASIC_init_methods(int id);
uint8_t ASIC_init(float frequency, uint8_t asic_count,uint16_t difficulty);
task_result * ASIC_process_work(bm_job ** active_jobs);
int ASIC_set_max_baud();
uint8_t ASIC_send_work(bm_job * next_job,bm_job ** active_jobs);
void ASIC_set_version_mask(uint32_t mask);
bool ASIC_set_frequency(float target_frequency);
double ASIC_get_asic_job_frequency_ms(uint32_t version_mask);

#endif // ASIC_H
