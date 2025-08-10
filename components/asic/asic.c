#include <string.h>
#include <esp_log.h>
#include "bm1397.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"
#include "asic.h"

static const char *TAG = "asic";

typedef struct {
    task_result *(*process_work)();
    int (*set_max_baud)();
    void (*send_work)(bm_job *next_job);
    void (*set_version_mask)(uint32_t mask);
    bool (*set_frequency)(float target_frequency);
    uint8_t (*asic_init)(uint64_t frequency, uint16_t asic_count, uint16_t difficulty);
} asic_methods_t;

static asic_methods_t asic_methods[] = {
    {BM1397_process_work, BM1397_set_max_baud, BM1397_send_work, BM1397_set_version_mask, NULL, BM1397_init},
    {BM1366_process_work, BM1366_set_max_baud, BM1366_send_work, BM1366_set_version_mask, BM1366_set_frequency, BM1366_init},
    {BM1368_process_work, BM1368_set_max_baud, BM1368_send_work, BM1368_set_version_mask, BM1368_set_frequency, BM1368_init},
    {BM1370_process_work, BM1370_set_max_baud, BM1370_send_work, BM1370_set_version_mask, BM1370_set_frequency, BM1370_init}
};

static asic_methods_t *current_asics;

void ASIC_init_methods(int id) {
    current_asics = &asic_methods[id];
}

uint8_t ASIC_init(float frequency, uint8_t _asic_count, uint16_t _difficulty) {
    ESP_LOGI(TAG, "Initializing %s", DEVICE_CONFIG.family.asic.name);
    return current_asics->asic_init(frequency, _asic_count, _difficulty);
}

task_result *ASIC_process_work() {
    return current_asics->process_work();
}

int ASIC_set_max_baud() {
    return current_asics->set_max_baud();
}

void ASIC_send_work(void *next_job) {
    current_asics->send_work(next_job);
}

void ASIC_set_version_mask(uint32_t mask) {
    current_asics->set_version_mask(mask);
}

bool ASIC_set_frequency(float target_frequency) {
    bool success = false;
    if (current_asics->set_frequency) {
        success = current_asics->set_frequency(target_frequency);
    }
    if (success) {
        ESP_LOGI(TAG, "Successfully transitioned to new ASIC frequency: %.2f MHz", target_frequency);
    } else {
        ESP_LOGE(TAG, "Failed to transition to new ASIC frequency: %.2f MHz", target_frequency);
    }
    return success;
}

