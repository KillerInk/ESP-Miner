#include "esp_log.h"
#include "esp_psram.h"
#include "adc.h"
#include "asic.h"
#include "asic_reset.h"
#include "asic_task.h"
#include "connect.h"
#include "device_config.h"
#include "http_server.h"
#include "i2c_bitaxe.h"
#include "nvs_device.h"
#include "self_test.h"
#include "serial.h"
#include "statistics_task.h"
#include "bap/bap.h"
#include "stratum_task.h"
#include "system.h"
#include "power_management_module.h"
#include "power_management_task.h"
#include "system_module.h"
#include "self_test_module.h"
#include "device_config.h"
#include "display.h"
#include "wifi_module.h"
#include "pool_module.h"
#include "state_module.h"

SystemModule SYSTEM_MODULE;
PowerManagementModule POWER_MANAGEMENT_MODULE;
DeviceConfig DEVICE_CONFIG;
DisplayConfig DISPLAY_CONFIG;
SelfTestModule SELF_TEST_MODULE;
StatisticsModule STATISTICS_MODULE;
WifiSettings WIFI_MODULE;
PoolModule POOL_MODULE;
StateModule STATE_MODULE;

static const char * TAG = "bitaxe";

#define DEFAULT_TASK_STACK_SIZE  8192
static StaticTask_t create_jobs_task_buffer;
static StackType_t create_jobs_task_stack[DEFAULT_TASK_STACK_SIZE];
static StaticTask_t stratum_task_buffer;
static StackType_t stratum_task_stack[DEFAULT_TASK_STACK_SIZE];
static StaticTask_t ASIC_result_task_buffer;
static StackType_t ASIC_result_task_stack[DEFAULT_TASK_STACK_SIZE];
static StaticTask_t statistics_task_buffer;
static StackType_t statistics_task_stack[DEFAULT_TASK_STACK_SIZE];

void app_main(void)
{
    ESP_LOGI(TAG, "Welcome to the bitaxe - FOSS || GTFO!");

    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "No PSRAM available on ESP32 device!");
        STATE_MODULE.psram_is_available = false;
    } else {
        STATE_MODULE.psram_is_available = true;
    }

    // Init I2C
    ESP_ERROR_CHECK(i2c_bitaxe_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    //wait for I2C to init
    vTaskDelay(100 / portTICK_PERIOD_MS);

    //Init ADC
    ADC_init();

    //initialize the ESP32 NVS
    if (NVSDevice_init() != ESP_OK){
        ESP_LOGE(TAG, "Failed to init NVS");
        return;
    }

    if (device_config_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init device config");
        return;
    }
    ASIC_init_methods(DEVICE_CONFIG.family.asic.id);
    if (self_test()) return;

    SYSTEM_init_system();
    statistics_init();

    // init AP and connect to wifi
    wifi_init();

    SYSTEM_init_peripherals();

    xTaskCreate(POWER_MANAGEMENT_task, "power management", 8192, NULL,10, NULL);
    // start the API for AxeOS
    start_rest_server();

    // Initialize BAP interface
    esp_err_t bap_ret = BAP_init();
    if (bap_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BAP interface: %d", bap_ret);
        // Continue anyway, as BAP is not critical for core functionality
    } else {
        ESP_LOGI(TAG, "BAP interface initialized successfully");
    }

    while (!WIFI_MODULE.is_connected) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    set_new_mining_notification_callback = set_new_mining_notification;
    stratum_submit_share_callback = stratum_submit_share;
    SYSTEM_notify_found_nonce_callback = SYSTEM_notify_found_nonce;
    set_extranonce_callback = set_extranonce;
    set_version_mask_callback = asic_task_set_version_mask;
    asic_task_init();
    
    if (asic_reset() != ESP_OK) {
        STATE_MODULE.asic_status = "ASIC reset failed";
        ESP_LOGE(TAG, "ASIC reset failed!");
        return;
    }

    SERIAL_init();

    ESP_LOGE(TAG, "ASIC_init id:%i count:%i diff:%i", DEVICE_CONFIG.family.asic.id, DEVICE_CONFIG.family.asic_count,
             DEVICE_CONFIG.family.asic.difficulty);
    if (ASIC_init(POWER_MANAGEMENT_MODULE.frequency_value, DEVICE_CONFIG.family.asic_count,
                  DEVICE_CONFIG.family.asic.difficulty) == 0) {
        STATE_MODULE.asic_status = "Chip count 0";
        ESP_LOGE(TAG, "Chip count 0");
        return;
    }

    SERIAL_set_baud(ASIC_set_max_baud());
    SERIAL_clear_buffer();

    STATE_MODULE.ASIC_initalized = true;
    xTaskCreateStatic(create_jobs_task, "stratum miner", DEFAULT_TASK_STACK_SIZE, NULL, 10, create_jobs_task_stack, &create_jobs_task_buffer);
    xTaskCreateStatic(stratum_task, "stratum admin", DEFAULT_TASK_STACK_SIZE, NULL, 5, stratum_task_stack,&stratum_task_buffer);
    xTaskCreateStatic(ASIC_result_task, "asic result", DEFAULT_TASK_STACK_SIZE, NULL, 15, ASIC_result_task_stack,&ASIC_result_task_buffer);
    xTaskCreateStatic(statistics_task, "statistics", DEFAULT_TASK_STACK_SIZE, NULL, 3, statistics_task_stack,&statistics_task_buffer);
}
