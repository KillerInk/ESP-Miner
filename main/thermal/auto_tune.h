#ifndef AUTO_TUNE_H_
#define AUTO_TUNE_H_
#include <stdint.h>
#include <stdbool.h>

void auto_tune_init();
void auto_tune(bool pid_control_fanspeed);
uint16_t auto_tune_get_frequency();
uint16_t auto_tune_get_voltage();
#endif