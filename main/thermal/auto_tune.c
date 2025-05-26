#include "auto_tune.h"
#include "esp_log.h"
#include "global_state.h"
#include "nvs_config.h"

static const char * TAG = "auto_tune";

uint16_t last_core_voltage_auto;
uint16_t last_asic_frequency_auto;
double last_hashrate_auto;
double current_hashrate_auto;

uint8_t auto_tune_counter = 0;
bool lastVoltageSet = false;

double autotune_power_limit = 38.;
uint16_t autotune_fan_limit = 80;
uint8_t autotune_step_volt = 1;
uint8_t autotune_step_freq_rampup = 5;
uint8_t autotune_step_freq = 2;
uint8_t autotune_step = 5;
uint8_t autotune_read_tick = 1;
const uint16_t max_voltage_asic = 1400;
const uint16_t max_frequency_asic = 1000;
const uint8_t max_asic_temperatur = 65;

uint16_t core_voltage;
uint16_t asic_frequency;

enum TuneState
{
    sleep_bevor_warmup,
    warmup,
    working
};

enum TuneState state;

void auto_tune_init()
{
    last_core_voltage_auto = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
    last_asic_frequency_auto = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);
    asic_frequency = last_asic_frequency_auto;
    core_voltage = last_core_voltage_auto;
    GLOBAL_STATE.POWER_MANAGEMENT_MODULE.core_voltage = last_core_voltage_auto;
    last_hashrate_auto = GLOBAL_STATE.SYSTEM_MODULE.current_hashrate;
    current_hashrate_auto = last_hashrate_auto;
    state = sleep_bevor_warmup;
}

bool waitForStartUp(bool pid_control_fanspeed)
{
    return current_hashrate_auto > 0 && pid_control_fanspeed && auto_tune_counter > 50;
}

bool should_do_work()
{
    return auto_tune_counter >= 53 || GLOBAL_STATE.POWER_MANAGEMENT_MODULE.chip_temp_avg >= max_asic_temperatur ||
           GLOBAL_STATE.POWER_MANAGEMENT_MODULE.fan_perc > autotune_fan_limit;
}

bool can_increase_values()
{
    return GLOBAL_STATE.POWER_MANAGEMENT_MODULE.fan_perc < autotune_fan_limit - 2 &&
           GLOBAL_STATE.POWER_MANAGEMENT_MODULE.power < autotune_power_limit;
}

bool hashrate_increase()
{
    return last_hashrate_auto < current_hashrate_auto;
}

void respectLimits()
{
    if (last_asic_frequency_auto >= max_frequency_asic) {
        last_asic_frequency_auto = max_frequency_asic;
        last_core_voltage_auto += autotune_step_volt;
        lastVoltageSet = true;
    }

    if (last_core_voltage_auto >= max_voltage_asic) {
        last_core_voltage_auto = max_voltage_asic;
        last_asic_frequency_auto += autotune_step;
        lastVoltageSet = false;
    }

    if (last_asic_frequency_auto >= max_frequency_asic)
        last_asic_frequency_auto = max_frequency_asic;
}

bool limithit()
{
    return GLOBAL_STATE.POWER_MANAGEMENT_MODULE.fan_perc >= autotune_fan_limit +3 ||
           GLOBAL_STATE.POWER_MANAGEMENT_MODULE.power >= autotune_power_limit ||
           GLOBAL_STATE.POWER_MANAGEMENT_MODULE.chip_temp_avg >= max_asic_temperatur;
}

bool critical_limithit()
{
    return GLOBAL_STATE.POWER_MANAGEMENT_MODULE.chip_temp_avg > max_asic_temperatur;
}

void increase_values()
{
    if (hashrate_increase()) {
        // last step was frequency step increase again
        if (!lastVoltageSet) {
            last_asic_frequency_auto += autotune_step;
        }
        // last set was to core voltage
        else {
            last_core_voltage_auto += autotune_step_volt;
        }
    }
    // hash rate decrased with last set
    else {
        // last set was voltage, increase now frequency
        if (lastVoltageSet) {
            last_asic_frequency_auto += autotune_step;
            lastVoltageSet = false;
        }
        // last set was to frequency, increase voltage
        else {
            last_core_voltage_auto += autotune_step_volt;
            lastVoltageSet = true;
        }
    }
    respectLimits();
}

void decrease_values()
{
    if (hashrate_increase()) {
        // last set was frequency
        if (!lastVoltageSet) {
            // decrease core voltage and hope that it helps to keep hashrate up
            last_core_voltage_auto -= autotune_step_volt;
            lastVoltageSet = true;
        } else {
            // decrase frequency
            last_asic_frequency_auto -= autotune_step;
            lastVoltageSet = false;
        }
    }
    // hashrate decrease
    else {
        // last set was voltage
        if (lastVoltageSet) {
            // undo voltage
            last_core_voltage_auto -= autotune_step_volt;
        } else {
            last_asic_frequency_auto -= autotune_step;
        }
    }
}

void dowork()
{
    
    if (should_do_work()) {
        auto_tune_counter = 51;
        if (can_increase_values()) {
            increase_values();
        } else if (limithit()) {
            if (!critical_limithit()) {
                decrease_values();
            } else {
                last_asic_frequency_auto -= autotune_step*2;
                last_core_voltage_auto -= autotune_step_volt*2;
            }
        }
        ESP_LOGI(TAG, "\n######### \n       voltage:%u frequency:%u hash last/cur:%f %f \n#########", last_core_voltage_auto,
                 last_asic_frequency_auto, last_hashrate_auto, current_hashrate_auto);

    } else {
        auto_tune_counter++;
    }
    /*if (last_hashrate_auto == 0)
        last_hashrate_auto = GLOBAL_STATE.SYSTEM_MODULE.current_hashrate;
    else
        last_hashrate_auto = 0.97 * last_hashrate_auto + 0.03 * GLOBAL_STATE.SYSTEM_MODULE.current_hashrate;*/
    last_hashrate_auto = current_hashrate_auto;
    GLOBAL_STATE.SYSTEM_MODULE.avg_hashrate = last_hashrate_auto;
    core_voltage = last_core_voltage_auto;
    asic_frequency = last_asic_frequency_auto;
}

void auto_tune(bool pid_control_fanspeed)
{
    current_hashrate_auto = GLOBAL_STATE.SYSTEM_MODULE.current_hashrate;
    switch (state) {
    case sleep_bevor_warmup: 
        if (waitForStartUp(pid_control_fanspeed))
            state = warmup;
        auto_tune_counter++;
        break;
    
    case warmup:
        autotune_step = autotune_step_freq_rampup;
        dowork();
        if(GLOBAL_STATE.POWER_MANAGEMENT_MODULE.fan_perc >= autotune_fan_limit)
            state = working;
    break;
    case working:
        autotune_step = autotune_step_freq;
        dowork();

        break;
    
    }
}

uint16_t auto_tune_get_frequency()
{
    return asic_frequency;
}

uint16_t auto_tune_get_voltage()
{
    return core_voltage;
}
