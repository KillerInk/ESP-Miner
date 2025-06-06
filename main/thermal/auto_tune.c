#include "auto_tune.h"
#include "esp_log.h"
#include "global_state.h"
#include "nvs_config.h"
#include "esp_timer.h"

static const char * TAG = "auto_tune";

auto_tune_settings AUTO_TUNE = {
    .power_limit=20, 
    .fan_limit=75,
    .step_volt = 1,
    .step_freq_rampup = 5,
    .step_freq = 1,
    .autotune_step_frequency = 5,
    .autotune_read_tick = 1,
    .max_voltage_asic = 1400,
    .max_frequency_asic = 1000,
    .max_asic_temperatur = 65,
    .frequency = 525,
    .voltage = 1150
};

uint16_t last_core_voltage_auto;
uint16_t last_asic_frequency_auto;
double last_hashrate_auto;
double current_hashrate_auto;
double avg_hashrate_auto;

bool lastVoltageSet = true;
const uint16_t sleeptime = 20 *1000;
long warumup_end_time;

enum TuneState
{
    sleep_bevor_warmup,
    warmup,
    working
};

enum TuneState state;

void auto_tune_init()
{
    AUTO_TUNE.frequency = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);
    AUTO_TUNE.voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
    AUTO_TUNE.power_limit = nvs_config_get_u16(NVS_CONFIG_POWER_LIMIT, 20);
    AUTO_TUNE.fan_limit = nvs_config_get_u16(NVS_CONFIG_POWER_LIMIT, 75);
    last_core_voltage_auto = AUTO_TUNE.voltage;
    last_asic_frequency_auto = AUTO_TUNE.frequency;
    POWER_MANAGEMENT_MODULE.core_voltage = last_core_voltage_auto;
    last_hashrate_auto = SYSTEM_MODULE.current_hashrate;
    current_hashrate_auto = last_hashrate_auto;
    state = sleep_bevor_warmup;
    warumup_end_time = esp_timer_get_time() + sleeptime;
}

bool waitForStartUp(bool pid_control_fanspeed)
{
    return current_hashrate_auto > 0 && pid_control_fanspeed && esp_timer_get_time() < warumup_end_time;
}

bool should_do_work()
{
    return POWER_MANAGEMENT_MODULE.chip_temp_avg >= AUTO_TUNE.max_asic_temperatur ||
           POWER_MANAGEMENT_MODULE.fan_perc > AUTO_TUNE.fan_limit;
}

bool can_increase_values()
{
    return POWER_MANAGEMENT_MODULE.fan_perc < AUTO_TUNE.fan_limit && POWER_MANAGEMENT_MODULE.power < AUTO_TUNE.power_limit &&
           POWER_MANAGEMENT_MODULE.chip_temp_avg < AUTO_TUNE.max_asic_temperatur;
}

bool hashrate_increase()
{
    return last_hashrate_auto < current_hashrate_auto;
}

void respectLimits()
{
    if (last_asic_frequency_auto >= AUTO_TUNE.max_frequency_asic) {
        last_asic_frequency_auto = AUTO_TUNE.max_frequency_asic;
        last_core_voltage_auto += AUTO_TUNE.step_volt;
        lastVoltageSet = true;
    }

    if (last_core_voltage_auto >= AUTO_TUNE.max_voltage_asic) {
        last_core_voltage_auto = AUTO_TUNE.max_voltage_asic;
        last_asic_frequency_auto += AUTO_TUNE.autotune_step_frequency;
        lastVoltageSet = false;
    }

    if (last_asic_frequency_auto >= AUTO_TUNE.max_frequency_asic)
        last_asic_frequency_auto = AUTO_TUNE.max_frequency_asic;
}

bool limithit()
{
    return POWER_MANAGEMENT_MODULE.fan_perc >= AUTO_TUNE.fan_limit + 5 ||
           POWER_MANAGEMENT_MODULE.power >= AUTO_TUNE.power_limit + 0.03 ||
           POWER_MANAGEMENT_MODULE.chip_temp_avg >= AUTO_TUNE.max_asic_temperatur;
}

bool critical_limithit()
{
    return POWER_MANAGEMENT_MODULE.chip_temp_avg > AUTO_TUNE.max_asic_temperatur;
}

void increase_values()
{
    if (hashrate_increase()) {
        // last step was frequency step increase again
        if (!lastVoltageSet) {
            last_asic_frequency_auto += AUTO_TUNE.autotune_step_frequency;
        }
        // last set was to core voltage
        else {
            last_core_voltage_auto += AUTO_TUNE.step_volt;
        }
    }
    // hash rate decrased with last set
    else {
        if (limithit()) { // last set was voltage, increase now frequency
            if (lastVoltageSet) {
                last_core_voltage_auto -= AUTO_TUNE.step_volt;

                lastVoltageSet = false;
            }
            // last set was to frequency, increase voltage
            else {
                last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency;
                lastVoltageSet = true;
            }
        } else { // last set was voltage, increase now frequency
            if (lastVoltageSet) {
                last_asic_frequency_auto += AUTO_TUNE.autotune_step_frequency;

                lastVoltageSet = false;
            }
            // last set was to frequency, increase voltage
            else {
                last_core_voltage_auto += AUTO_TUNE.step_volt;
                
                lastVoltageSet = true;
            }
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
            last_core_voltage_auto -= AUTO_TUNE.step_volt * 2;

        } else {
            // decrase frequency
            last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency * 2;
        }
    }
    // hashrate decrease
    else {
        // last set was voltage
        if (lastVoltageSet) {
            // undo voltage
            last_core_voltage_auto -= AUTO_TUNE.step_volt * 2;
        } else {
            last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency * 2;
        }
    }
}

void dowork()
{

    if (should_do_work()) {
        if (can_increase_values()) {
            increase_values();
        } else if (limithit()) {
            if (!critical_limithit()) {
                decrease_values();
            } else {
                last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency * 2;
                last_core_voltage_auto -= AUTO_TUNE.step_volt * 2;
            }
        }
        // ESP_LOGI(TAG, "\n######### \n       voltage:%u frequency:%u hash last/cur:%f %f \n#########", last_core_voltage_auto,
        //          last_asic_frequency_auto, last_hashrate_auto, current_hashrate_auto);

    }
    if (avg_hashrate_auto == 0)
        avg_hashrate_auto = SYSTEM_MODULE.current_hashrate;
    else
        avg_hashrate_auto = 0.997 * avg_hashrate_auto + 0.003 * SYSTEM_MODULE.current_hashrate;
    last_hashrate_auto = current_hashrate_auto;
    SYSTEM_MODULE.avg_hashrate = avg_hashrate_auto;
    AUTO_TUNE.voltage = last_core_voltage_auto;
    AUTO_TUNE.frequency = last_asic_frequency_auto;
}

void auto_tune(bool pid_control_fanspeed)
{
    current_hashrate_auto = SYSTEM_MODULE.current_hashrate;
    switch (state) {
    case sleep_bevor_warmup:
        if (waitForStartUp(pid_control_fanspeed))
            state = warmup;
        break;

    case warmup:
        AUTO_TUNE.autotune_step_frequency = AUTO_TUNE.step_freq_rampup;
        dowork();
        if (limithit()) {
            AUTO_TUNE.autotune_step_frequency = AUTO_TUNE.step_freq;
            state = working;
        }
        break;
    case working:
        dowork();
        break;
    }
}

uint16_t auto_tune_get_frequency()
{
    return AUTO_TUNE.frequency;
}

uint16_t auto_tune_get_voltage()
{
    return AUTO_TUNE.voltage;
}
