#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mining_module.h"
#include "device_config.h"
#include "asic_task_module.h"
#include "asic.h"
#include "system.h"
#include "power_management_module.h"

static const char *TAG = "asic_task";

// static bm_job ** active_jobs; is required to keep track of the active jobs since the
const double NONCE_SPACE = 4294967296.0; //  2^32

double ASIC_get_asic_job_frequency_ms(float frequency)
{
    switch (DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            // no version-rolling so same Nonce Space is splitted between Small Cores
            return (NONCE_SPACE / (double) (frequency * DEVICE_CONFIG.family.asic.small_core_count * 1000)) / (double) DEVICE_CONFIG.family.asic_count;
        case BM1366:
            return 2000;
        default:
            return 500;
    }
}
void ASIC_task(void *pvParameters) {
    ASIC_TASK_MODULE.active_jobs = malloc(sizeof(bm_job *) * 128);
    ASIC_TASK_MODULE.valid_jobs = malloc(sizeof(uint8_t) * 128);
    for (int i = 0; i < 128; i++) {
        ASIC_TASK_MODULE.active_jobs[i] = NULL;
        ASIC_TASK_MODULE.valid_jobs[i] = 0;
    }

    double asic_job_frequency_ms = ASIC_get_asic_job_frequency_ms(POWER_MANAGEMENT_MODULE.frequency_value);

    ESP_LOGI(TAG, "ASIC Job Interval: %.2f ms", asic_job_frequency_ms);
    SYSTEM_notify_mining_started();
    ESP_LOGI(TAG, "ASIC Ready!");

    while (1) {
        bm_job *next_bm_job = queue_dequeue(&MINING_MODULE.ASIC_jobs_queue);
        if(next_bm_job != NULL)
        ASIC_send_work(next_bm_job);

        vTaskDelay(asic_job_frequency_ms / portTICK_PERIOD_MS);
    }
}
