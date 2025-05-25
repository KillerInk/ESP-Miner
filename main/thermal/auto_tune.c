#include "auto_tune.h"
#include "global_state.h"
#include "esp_log.h"
#include "nvs_config.h"

static const char * TAG = "auto_tune";

uint16_t last_core_voltage_auto;
uint16_t last_asic_frequency_auto;
double last_hashrate_auto;

uint8_t auto_tune_counter = 0;
bool lastVoltageSet = false;

double autotune_power_limit = 38.;
uint16_t autotune_fan_limit = 80;
uint8_t autotune_step = 2;
uint8_t autotune_read_tick = 1;
const uint16_t max_voltage_asic = 1400;
const uint16_t max_frequency_asic = 1000;
const uint8_t max_asic_temperatur = 65;

uint16_t core_voltage;
uint16_t asic_frequency;

PowerManagementModule * power_management;
SystemModule * sys_module;

void auto_tune_init()
{
    power_management = &GLOBAL_STATE.POWER_MANAGEMENT_MODULE;
    sys_module = &GLOBAL_STATE.SYSTEM_MODULE;
    last_core_voltage_auto = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
    power_management->core_voltage = last_core_voltage_auto;
    last_hashrate_auto = sys_module->current_hashrate;
}

void auto_tune(bool pid_control_fanspeed)
{
    if (sys_module->current_hashrate > 0 && pid_control_fanspeed && auto_tune_counter > 50) {
        // enter when its tick time or emergency due high asic temp
        if (auto_tune_counter >= 53 || power_management->chip_temp_avg >= max_asic_temperatur ||
            power_management->fan_perc > autotune_fan_limit) {
            auto_tune_counter = 51;
            // speed up if fan ist below max fan limit -2. in that 2%range do nothing, most time happy hashing.
            if (power_management->fan_perc < autotune_fan_limit - 2 && power_management->power < autotune_power_limit) {
                // current hash is higher then the old one
                if (last_hashrate_auto < sys_module->current_hashrate) {
                    // last step was frequency step increase again
                    if (!lastVoltageSet) {
                        last_asic_frequency_auto += autotune_step;
                    }
                    // last set was to core voltage
                    else {
                        last_core_voltage_auto += autotune_step - 1;
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
                        last_core_voltage_auto += autotune_step - 1;
                        lastVoltageSet = true;
                    }
                }

                if (last_asic_frequency_auto >= max_frequency_asic) {
                    last_asic_frequency_auto = max_frequency_asic;
                    last_core_voltage_auto += autotune_step - 1;
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
            // we hit a limit
            else if (power_management->fan_perc >= autotune_fan_limit || power_management->power >= autotune_power_limit ||
                     power_management->chip_temp_avg >= max_asic_temperatur) {
                // fan limit reached do normal throttle, asic is in temp range
                if (power_management->chip_temp_avg < max_asic_temperatur) {
                    // hasrate increasing
                    if (last_hashrate_auto < sys_module->current_hashrate) {
                        // last set was frequency
                        if (!lastVoltageSet) {
                            // decrease core voltage and hope that it helps to keep hashrate up
                            last_core_voltage_auto -= autotune_step - 1;
                        } else {
                            // decrase frequency
                            last_asic_frequency_auto -= autotune_step;
                        }
                    }
                    // hashrate decrease
                    else {
                        // last set was voltage
                        if (lastVoltageSet) {
                            // undo voltage
                            last_core_voltage_auto -= autotune_step - 1;
                        } else {
                            last_asic_frequency_auto -= autotune_step;
                        }
                    }
                } else {
                    last_asic_frequency_auto -= autotune_step;
                    last_core_voltage_auto -= autotune_step;
                }
            }
            ESP_LOGI(TAG, "\n######### \n       voltage:%u frequency:%u hash last/cur:%f %f \n#########", last_core_voltage_auto,
                     last_asic_frequency_auto, last_hashrate_auto, sys_module->current_hashrate);

        } else
            auto_tune_counter++;
    } else {
        auto_tune_counter++;
    }
    if (last_hashrate_auto == 0)
        last_hashrate_auto = sys_module->current_hashrate;
    else
        last_hashrate_auto = 0.93 * last_hashrate_auto + 0.07 * sys_module->current_hashrate;
    sys_module->avg_hashrate = last_hashrate_auto;
    core_voltage = last_core_voltage_auto;
    asic_frequency = last_asic_frequency_auto;
}

uint16_t auto_tune_get_frequency()
{
    return asic_frequency;
}

uint16_t auto_tune_get_voltage()
{
    return core_voltage;
}
