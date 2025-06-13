#include "auto_tune.h"
#include "esp_log.h"
#include "global_state.h"
#include "nvs_config.h"
#include "PID.h"
#include "power_management_task.h"

static const char * TAG = "auto_tune";

auto_tune_settings AUTO_TUNE = {.power_limit = 20,
                                .fan_limit = 75,
                                .step_volt = 1,
                                .step_freq_rampup = 4,
                                .step_freq = 2,
                                .autotune_step_frequency = 0,
                                .max_voltage_asic = 1400,
                                .max_frequency_asic = 1000,
                                .max_asic_temperatur = 65,
                                .frequency = 525,
                                .voltage = 1150};

double last_core_voltage_auto;
double last_asic_frequency_auto;
double last_hashrate_auto;
double current_hashrate_auto;
double avg_hashrate_auto;

bool lastVoltageSet = false;
const int waitTime = 30;
int waitCounter = 0;
int falling_diff;
int last_falling_diff;

enum TuneState
{
    sleep_before_warmup,
    warmup,
    working
};

enum TuneState state;

void auto_tune_init()
{
    AUTO_TUNE.frequency = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);
    AUTO_TUNE.voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
    AUTO_TUNE.power_limit = nvs_config_get_u16(NVS_CONFIG_POWER_LIMIT, 20);
    AUTO_TUNE.fan_limit = nvs_config_get_u16(NVS_CONFIG_FAN_LIMIT, 75);
    last_core_voltage_auto = AUTO_TUNE.voltage;
    last_asic_frequency_auto = AUTO_TUNE.frequency;
    POWER_MANAGEMENT_MODULE.core_voltage = last_core_voltage_auto;
    last_hashrate_auto = SYSTEM_MODULE.current_hashrate;
    current_hashrate_auto = last_hashrate_auto;
    state = sleep_before_warmup;
    waitCounter = 45 * 1000 / POLL_RATE;
}

bool waitForStartUp(bool pid_control_fanspeed)
{
    return current_hashrate_auto > 0 && pid_control_fanspeed && waitCounter <= 0;
}

bool can_increase_values()
{
    return POWER_MANAGEMENT_MODULE.fan_perc < AUTO_TUNE.fan_limit && POWER_MANAGEMENT_MODULE.power < AUTO_TUNE.power_limit &&
           POWER_MANAGEMENT_MODULE.chip_temp_avg < AUTO_TUNE.max_asic_temperatur;
}

bool hashrate_increase()
{
    return last_hashrate_auto > current_hashrate_auto;
}

void respectLimits()
{
    if (last_asic_frequency_auto > AUTO_TUNE.max_frequency_asic)
        last_asic_frequency_auto = AUTO_TUNE.max_frequency_asic;
    if (last_core_voltage_auto > AUTO_TUNE.max_voltage_asic)
        last_core_voltage_auto = AUTO_TUNE.max_voltage_asic;

    if (last_asic_frequency_auto < 401) {
        last_asic_frequency_auto = 525;
        last_core_voltage_auto = 1150;
        lastVoltageSet = false;
    }
    if (last_core_voltage_auto < 1001) {
        last_asic_frequency_auto = 525;
        last_core_voltage_auto = 1150;
        lastVoltageSet = true;
    }
}

bool limithit()
{
    return POWER_MANAGEMENT_MODULE.fan_perc >= AUTO_TUNE.fan_limit || POWER_MANAGEMENT_MODULE.power >= AUTO_TUNE.power_limit ||
           POWER_MANAGEMENT_MODULE.chip_temp_avg >= AUTO_TUNE.max_asic_temperatur;
}

bool critical_limithit()
{
    return POWER_MANAGEMENT_MODULE.chip_temp_avg > AUTO_TUNE.max_asic_temperatur || POWER_MANAGEMENT_MODULE.power >= AUTO_TUNE.power_limit +0.5 ||
           POWER_MANAGEMENT_MODULE.fan_perc >= AUTO_TUNE.fan_limit + 5;
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

    } else {
        if (!lastVoltageSet) {
            last_asic_frequency_auto += AUTO_TUNE.autotune_step_frequency;
        }
        // last set was to core voltage
        else {
            last_core_voltage_auto += AUTO_TUNE.step_volt;
        }
        lastVoltageSet = !lastVoltageSet;
    }
}

void decrease_values()
{
    if (hashrate_increase()) {
        if (!lastVoltageSet) {
            // decrease core voltage and hope that it helps to keep hashrate up
            last_core_voltage_auto -= AUTO_TUNE.step_volt;

        } else {
            // decrase frequency
            last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency;
        }
    } else {
       
        if (!lastVoltageSet) {
            // decrease core voltage and hope that it helps to keep hashrate up
            last_core_voltage_auto -= AUTO_TUNE.step_volt;

        } else {
            // decrase frequency
            last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency;
        }
        lastVoltageSet = !lastVoltageSet;
    }
    
}

void switchvalue()
{
    if (hashrate_increase()) {
        if (!lastVoltageSet) {
            // decrease core voltage and hope that it helps to keep hashrate up
            last_core_voltage_auto -= AUTO_TUNE.step_volt;
            last_asic_frequency_auto += AUTO_TUNE.autotune_step_frequency;

        } else {
            // decrase frequency
            last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency;
            last_core_voltage_auto += AUTO_TUNE.step_volt;
        }
    } else {
        lastVoltageSet = !lastVoltageSet;
    }
    
}

void dowork()
{
    if (avg_hashrate_auto == 0)
        avg_hashrate_auto = SYSTEM_MODULE.current_hashrate;
    else
        avg_hashrate_auto = 0.999 * avg_hashrate_auto + 0.001 * SYSTEM_MODULE.current_hashrate;
    falling_diff = last_hashrate_auto - current_hashrate_auto;
    if (limithit()) {
        if (!critical_limithit()) {
            switchvalue();
        } else {
            last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency*2;
            last_core_voltage_auto -= AUTO_TUNE.step_volt*2;
        }
    } else if(can_increase_values()) {
        increase_values();
    }
    else decrease_values();

    
    ESP_LOGI(TAG, "Diff %i Hashrate %f Voltage %f Frequency %f", falling_diff, 
             avg_hashrate_auto, last_core_voltage_auto, last_asic_frequency_auto);
    
    last_hashrate_auto = current_hashrate_auto;
    respectLimits();
    last_falling_diff = falling_diff;
    SYSTEM_MODULE.avg_hashrate = avg_hashrate_auto;
    AUTO_TUNE.voltage = last_core_voltage_auto;
    AUTO_TUNE.frequency = last_asic_frequency_auto;
}

void auto_tune(bool pid_control_fanspeed)
{
    current_hashrate_auto = SYSTEM_MODULE.current_hashrate;
    switch (state) {
    case sleep_before_warmup:
        if (POWER_MANAGEMENT_MODULE.chip_temp_avg == -1) {
            break;
        }

        if (waitCounter-- > 0) {
            ESP_LOGI(TAG, "state sleep_bevor_warmup %i", waitCounter);
            break;
        }

        if (waitForStartUp(pid_control_fanspeed))
            state = warmup;
        break;

    case warmup:
        // ESP_LOGI(TAG, "state_warmup");
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

double auto_tune_get_frequency()
{
    return AUTO_TUNE.frequency;
}

double auto_tune_get_voltage()
{
    return AUTO_TUNE.voltage;
}
