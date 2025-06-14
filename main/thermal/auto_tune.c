#include "auto_tune.h"
#include "PID.h"
#include "esp_log.h"
#include "global_state.h"
#include "nvs_config.h"
#include "power_management_task.h"
#include <math.h>

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

enum TuneState
{
    sleep_before_warmup,
    warmup,
    working
};

enum TuneState state;

static int tuning_cycle_count = 0;

void auto_tune_init()
{
    AUTO_TUNE.frequency = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, AUTO_TUNE.frequency);
    AUTO_TUNE.voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, AUTO_TUNE.voltage);
    AUTO_TUNE.power_limit = nvs_config_get_u16(NVS_CONFIG_KEY_POWER_LIMIT, AUTO_TUNE.power_limit);
    AUTO_TUNE.fan_limit = nvs_config_get_u16(NVS_CONFIG_KEY_FAN_LIMIT, AUTO_TUNE.fan_limit);
    AUTO_TUNE.max_voltage_asic = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_VOLTAGE_ASIC, AUTO_TUNE.max_voltage_asic);
    AUTO_TUNE.max_frequency_asic = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_FREQUENCY_ASIC, AUTO_TUNE.max_frequency_asic);
    AUTO_TUNE.max_asic_temperatur = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_ASIC_TEMPERATUR, AUTO_TUNE.max_asic_temperatur);

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
    return POWER_MANAGEMENT_MODULE.fan_perc < AUTO_TUNE.fan_limit - 1 &&
           POWER_MANAGEMENT_MODULE.power < AUTO_TUNE.power_limit - 0.1 &&
           POWER_MANAGEMENT_MODULE.chip_temp_avg < AUTO_TUNE.max_asic_temperatur;
}

static double get_hashrate_factor()
{
    double diff = current_hashrate_auto - last_hashrate_auto;
    if (last_hashrate_auto == 0)
        return 1.0;

    double base = 1.0 + (diff / last_hashrate_auto);

    if (diff > 0) {
        base *= 1.1;
    } else if (diff < 0) {
        base *= 1.2;
    }

    if (base < 0.8)
        base = 0.8;
    if (base > 1.5)
        base = 1.5;
    return base;
}

bool limithit()
{
    return POWER_MANAGEMENT_MODULE.fan_perc >= AUTO_TUNE.fan_limit || POWER_MANAGEMENT_MODULE.power >= AUTO_TUNE.power_limit ||
           POWER_MANAGEMENT_MODULE.chip_temp_avg >= AUTO_TUNE.max_asic_temperatur;
}

bool critical_limithit()
{
    return POWER_MANAGEMENT_MODULE.chip_temp_avg > AUTO_TUNE.max_asic_temperatur ||
           POWER_MANAGEMENT_MODULE.power >= AUTO_TUNE.power_limit + 0.5 ||
           POWER_MANAGEMENT_MODULE.fan_perc >= AUTO_TUNE.fan_limit + 5;
}

bool hashrate_decreased()
{
    return last_hashrate_auto > current_hashrate_auto;
}

// Helper to clamp a value between min and max
static inline double clamp(double val, double min, double max)
{
    if (val < min)
        return min;
    if (val > max)
        return max;
    return val;
}

static void enforce_voltage_frequency_ratio()
{
    double expected_voltage = last_asic_frequency_auto * 2.0;
    double lower_v = expected_voltage * 0.95;
    double upper_v = expected_voltage * 1.05;

    // If voltage is too high for frequency, bring voltage down
    if (last_core_voltage_auto > upper_v) {
        last_core_voltage_auto = upper_v;
    }
    // If frequency is too high for voltage, bring frequency down
    double expected_frequency = last_core_voltage_auto / 2.0;
    double upper_f = expected_frequency * 1.05;
    if (last_asic_frequency_auto > upper_f) {
        last_asic_frequency_auto = upper_f;
    }
}

// Unified adjustment function
static void adjust_value(double step_freq, double step_volt, bool increase)
{
    if (!lastVoltageSet) {
        last_asic_frequency_auto += (increase ? step_freq : -step_freq);
        enforce_voltage_frequency_ratio();
    } else {
        last_core_voltage_auto += (increase ? step_volt : -step_volt);
    }
    lastVoltageSet = !lastVoltageSet;
}

void increase_values()
{
    adjust_value(AUTO_TUNE.autotune_step_frequency, AUTO_TUNE.step_volt, true);
}

void decrease_values()
{
    adjust_value(AUTO_TUNE.autotune_step_frequency, AUTO_TUNE.step_volt, false);
}

void switchvalue()
{
    bool decrease = hashrate_decreased();

    double freq_step = AUTO_TUNE.autotune_step_frequency;
    double volt_step = AUTO_TUNE.step_volt * 2;

    if (decrease) {
        // If last step was voltage, try increasing frequency next
        if (lastVoltageSet) {
            last_asic_frequency_auto += freq_step;
            enforce_voltage_frequency_ratio();
        } else {
            last_core_voltage_auto += volt_step;
        }
    } else {
        // If hashrate dropped, revert the last change more aggressively
        if (lastVoltageSet) {
            // Last change was frequency, so reduce frequency
            last_asic_frequency_auto -= freq_step * 2;
            enforce_voltage_frequency_ratio();
        } else {
            // Last change was voltage, so reduce voltage
            last_core_voltage_auto -= volt_step * 2;
        }
    }
    lastVoltageSet = !lastVoltageSet;
}

void respectLimits()
{
    last_asic_frequency_auto = clamp(last_asic_frequency_auto, 401, AUTO_TUNE.max_frequency_asic);
    last_core_voltage_auto = clamp(last_core_voltage_auto, 1001, AUTO_TUNE.max_voltage_asic);

    if (last_asic_frequency_auto == 401 || last_core_voltage_auto == 1001) {
        last_asic_frequency_auto = 525;
        last_core_voltage_auto = 1150;
        lastVoltageSet = (last_core_voltage_auto == 1150);
    }
}

void dowork()
{
    tuning_cycle_count++;

    double decay = 1.0 / (1.0 + tuning_cycle_count * 0.005);
    double factor = get_hashrate_factor();
    AUTO_TUNE.autotune_step_frequency = fmax(AUTO_TUNE.step_freq * decay * factor, 0.5);
    AUTO_TUNE.step_volt = fmax((decay * factor), 0.2);

    avg_hashrate_auto = (avg_hashrate_auto == 0) ? SYSTEM_MODULE.current_hashrate
                                                 : 0.999 * avg_hashrate_auto + 0.001 * SYSTEM_MODULE.current_hashrate;

    if (limithit()) {
        if (!critical_limithit()) {
            switchvalue();
        } else {
            last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency * 2;
            last_core_voltage_auto -= AUTO_TUNE.step_volt * 2;
        }
    } else if (can_increase_values()) {
        increase_values();
    } else {
        decrease_values();
    }

    ESP_LOGI(TAG, "Hashrate %f Voltage %f Frequency %f", avg_hashrate_auto, last_core_voltage_auto, last_asic_frequency_auto);

    last_hashrate_auto = current_hashrate_auto;
    respectLimits();
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
