#include "INA260.h"
#include "PID.h"
#include "TPS546.h"
#include "asic.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "math.h"
#include "mining.h"
#include "nvs_config.h"
#include "power.h"
#include "serial.h"
#include "thermal.h"
#include "vcore.h"
#include <string.h>

#define POLL_RATE 1800
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define THROTTLE_TEMP_RANGE (MAX_TEMP - THROTTLE_TEMP)

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

#define TPS546_THROTTLE_TEMP 105.0
#define TPS546_MAX_TEMP 145.0

static const char * TAG = "power_management";

double pid_input = 0.0;
double pid_output = 0.0;
double pid_setPoint = 60.0;
double pid_p = 4.0;
double pid_i = 0.2;
double pid_d = 3.0;

double autotune_power_limit = 30.;
uint16_t autotune_fan_limit = 80;
uint8_t autotune_step = 2;
uint8_t autotune_read_tick = 2;
const uint16_t max_voltage_asic = 1500;
const uint16_t max_frequency_asic = 1200;
const uint8_t max_asic_temperatur = 65;

PIDController pid;

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    // Initialize PID controller
    pid_setPoint = (double) nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET, pid_setPoint);
    pid_init(&pid, &pid_input, &pid_output, &pid_setPoint, pid_p, pid_i, pid_d, PID_P_ON_E, PID_DIRECT);
    pid_set_sample_time(&pid, POLL_RATE - 1);
    pid_set_output_limits(&pid, 25, 100);
    pid_set_mode(&pid, AUTOMATIC);
    pid_set_controller_direction(&pid, PID_REVERSE);
    pid_initialize(&pid);

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;

    power_management->frequency_multiplier = 1;

    // int last_frequency_increase = 0;
    // uint16_t frequency_target = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);

    vTaskDelay(500 / portTICK_PERIOD_MS);
    uint16_t last_core_voltage = 0.0;
    uint16_t last_asic_frequency = power_management->frequency_value;
    uint16_t last_core_voltage_auto = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
    power_management->core_voltage = last_core_voltage_auto;
    uint16_t last_asic_frequency_auto = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);
    double last_hashrate_auto = sys_module->current_hashrate;
    bool auto_tune_hashrate = true;
    uint8_t auto_tune_counter = 0;

    while (1) {

        // Refresh PID setpoint from NVS in case it was changed via API
        pid_setPoint = (double) nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET, pid_setPoint);

        power_management->voltage = Power_get_input_voltage(GLOBAL_STATE);
        power_management->power = Power_get_power(GLOBAL_STATE);

        power_management->fan_rpm = Thermal_get_fan_speed(GLOBAL_STATE->DEVICE_CONFIG);
        power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);

        power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);

        // ASIC Thermal Diode will give bad readings if the ASIC is turned off
        // if(power_management->voltage < tps546_config.TPS546_INIT_VOUT_MIN){
        //     goto looper;
        // }

        // overheat mode if the voltage regulator or ASIC is too hot
        if ((power_management->vr_temp > TPS546_THROTTLE_TEMP || power_management->chip_temp_avg > THROTTLE_TEMP) &&
            (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
            auto_tune_hashrate = false;
            ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC %fC", power_management->vr_temp, power_management->chip_temp_avg);
            power_management->fan_perc = 100;
            Thermal_set_fan_percent(GLOBAL_STATE->DEVICE_CONFIG, 1);

            // Turn off core voltage
            VCORE_set_voltage(0.0f, GLOBAL_STATE);

            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1000);
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 50);
            nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
            nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);
            nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 1);
            exit(EXIT_FAILURE);
        }
        bool pid_control_fanspeed = nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1) == 1;
        // enable the PID auto control for the FAN if set
        if (pid_control_fanspeed) {
            // Ignore invalid temperature readings (-1) during startup
            if (power_management->chip_temp_avg >= 0) {
                pid_input = power_management->chip_temp_avg;
                pid_compute(&pid);
                power_management->fan_perc = (uint16_t) pid_output;
                Thermal_set_fan_percent(GLOBAL_STATE->DEVICE_CONFIG, pid_output / 100.0);
                ESP_LOGI(TAG, "Temp: %.1f°C, SetPoint: %.1f°C, Output: %.1f%%", pid_input, pid_setPoint, pid_output);
            } else {
                // Set fan to 70% in AP mode when temperature reading is invalid
                if (GLOBAL_STATE->SYSTEM_MODULE.ap_enabled) {
                    ESP_LOGW(TAG, "AP mode with invalid temperature reading: %.1f°C - Setting fan to 70%%",
                             power_management->chip_temp_avg);
                    power_management->fan_perc = 70;
                    Thermal_set_fan_percent(GLOBAL_STATE->DEVICE_CONFIG, 0.7);
                } else {
                    ESP_LOGW(TAG, "Ignoring invalid temperature reading: %.1f°C", power_management->chip_temp_avg);
                }
            }
        } else {
            float fs = (float) nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100);
            power_management->fan_perc = fs;
            Thermal_set_fan_percent(GLOBAL_STATE->DEVICE_CONFIG, (float) fs / 100.0);
        }

        // Read the state of plug sense pin
        // if (power_management->HAS_PLUG_SENSE) {
        //     int gpio_plug_sense_state = gpio_get_level(GPIO_PLUG_SENSE);
        //     if (gpio_plug_sense_state == 0) {
        //         // turn ASIC off
        //         gpio_set_level(GPIO_ASIC_ENABLE, 1);
        //     }
        // }

        // New voltage and frequency adjustment code
        uint16_t core_voltage;
        uint16_t asic_frequency;

        if (!auto_tune_hashrate) {
            core_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
            asic_frequency = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);
        } else {
            if (sys_module->current_hashrate > 0 && pid_control_fanspeed) {
                if (auto_tune_counter >= autotune_read_tick || power_management->chip_temp_avg >= max_asic_temperatur) {
                    auto_tune_counter = 0;
                    if (power_management->fan_perc < autotune_fan_limit && power_management->power < autotune_power_limit) {
                        if (last_hashrate_auto < sys_module->current_hashrate)
                            last_asic_frequency_auto += autotune_step;
                        else
                            last_core_voltage_auto += autotune_step;

                    if(last_asic_frequency_auto >= max_frequency_asic)
                        last_asic_frequency_auto = max_frequency_asic;

                    if(last_core_voltage_auto >= max_voltage_asic)
                        last_core_voltage_auto = max_voltage_asic;

                    } else if (power_management->fan_perc >= autotune_fan_limit || power_management->power >= autotune_power_limit || power_management->chip_temp_avg >= max_asic_temperatur) {
                        if(power_management->chip_temp_avg < max_asic_temperatur)
                        {
                        if (last_hashrate_auto > sys_module->current_hashrate)
                            last_asic_frequency_auto -= autotune_step;
                        else
                            last_core_voltage_auto -= autotune_step;
                        }
                        else
                        {
                            last_asic_frequency_auto -= autotune_step;
                            last_core_voltage_auto -= autotune_step*2;
                        }
                    }
                    ESP_LOGI(TAG, "\n######### \n       voltage:%u frequency:%u hash last/cur:%f %f \n#########",
                             last_core_voltage_auto, last_asic_frequency_auto, last_hashrate_auto, sys_module->current_hashrate);
                    if (last_hashrate_auto == 0)
                        last_hashrate_auto = sys_module->current_hashrate;
                    else
                        last_hashrate_auto = 0.93 * last_hashrate_auto + 0.07 * sys_module->current_hashrate;
                } else {
                    auto_tune_counter++;
                }
            }
            core_voltage = last_core_voltage_auto;
            asic_frequency = last_asic_frequency_auto;
        }

        if (core_voltage != last_core_voltage) {
            ESP_LOGI(TAG, "setting new vcore voltage to %umV", core_voltage);
            VCORE_set_voltage((double) core_voltage / 1000.0, GLOBAL_STATE);
            last_core_voltage = core_voltage;
            power_management->core_voltage = core_voltage;
        }

        if (asic_frequency != last_asic_frequency) {
            ESP_LOGI(TAG, "New ASIC frequency requested: %uMHz (current: %uMHz)", asic_frequency, last_asic_frequency);

            bool success = ASIC_set_frequency(GLOBAL_STATE, (float) asic_frequency);

            if (success) {
                power_management->frequency_value = (float) asic_frequency;
            }

            last_asic_frequency = asic_frequency;
        }

        // Check for changing of overheat mode
        uint16_t new_overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0);

        if (new_overheat_mode != sys_module->overheat_mode) {
            sys_module->overheat_mode = new_overheat_mode;
            ESP_LOGI(TAG, "Overheat mode updated to: %d", sys_module->overheat_mode);
        }

        VCORE_check_fault(GLOBAL_STATE);

        // looper:
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
