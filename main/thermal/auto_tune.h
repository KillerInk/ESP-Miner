#ifndef AUTO_TUNE_H_
#define AUTO_TUNE_H_
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    double power_limit;
    uint16_t fan_limit;
    uint8_t step_volt;
    uint8_t step_freq_rampup;
    uint8_t step_freq;
    uint8_t autotune_step_frequency;
    uint8_t autotune_read_tick;
    uint16_t max_voltage_asic;
    uint16_t max_frequency_asic;
    uint8_t max_asic_temperatur;
    uint16_t frequency;
    uint16_t voltage;
} auto_tune_settings;

extern auto_tune_settings AUTO_TUNE;
void auto_tune_init();
void auto_tune(bool pid_control_fanspeed);
uint16_t auto_tune_get_frequency();
uint16_t auto_tune_get_voltage();
#endif