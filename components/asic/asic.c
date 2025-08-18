#include <string.h>
#include <esp_log.h>
#include "bm1397.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"
#include "asic.h"
#include "device_config.h"
#include "frequency_transition_bmXX.h"
#include "mining_module.h"
#include "power_management_module.h"

static const char *TAG = "asic";

typedef struct {
    task_result *(*process_work)(bm_job ** active_jobs);
    int (*set_max_baud)();
    uint8_t (*send_work)(bm_job * next_job, bm_job ** active_jobs);
    void (*set_version_mask)(uint32_t mask);
    void (*set_frequency)(float target_frequency);
    uint8_t (*asic_init)(uint64_t frequency, uint16_t asic_count, uint16_t difficulty);
    void (*set_nonce_percent)(uint64_t frequency, uint16_t chain_chip_count);
    float (*get_timeout)(uint64_t frequency, uint16_t chain_chip_count, int versions_to_roll);
} asic_methods_t;

static asic_methods_t asic_methods[] = {
    {BM1397_process_work, BM1397_set_max_baud, BM1397_send_work, BM1397_set_version_mask, BM1397_send_hash_frequency, BM1397_init, BM1397_set_nonce_percent, BM1397_get_timeout},
    {BM1366_process_work, BM1366_set_max_baud, BM1366_send_work, BM1366_set_version_mask, BM1366_send_hash_frequency, BM1366_init, BM1366_set_nonce_percent, BM1366_get_timeout},
    {BM1368_process_work, BM1368_set_max_baud, BM1368_send_work, BM1368_set_version_mask, BM1368_send_hash_frequency, BM1368_init, BM1368_set_nonce_percent, BM1368_get_timeout},
    {BM1370_process_work, BM1370_set_max_baud, BM1370_send_work, BM1370_set_version_mask, BM1370_send_hash_frequency, BM1370_init, BM1370_set_nonce_percent, BM1370_get_timeout}
};

static asic_methods_t *current_asics;

void ASIC_init_methods(int id) {
    current_asics = &asic_methods[id];
}

uint8_t ASIC_init(float frequency, uint8_t _asic_count, uint16_t _difficulty) {
    ESP_LOGI(TAG, "Initializing %s", DEVICE_CONFIG.family.asic.name);
    return current_asics->asic_init(frequency, _asic_count, _difficulty);
}

task_result *ASIC_process_work(bm_job ** active_jobs) {
    return current_asics->process_work(active_jobs);
}

int ASIC_set_max_baud() {
    return current_asics->set_max_baud();
}

uint8_t ASIC_send_work(bm_job * next_job,bm_job ** active_jobs) {
    return current_asics->send_work(next_job,active_jobs);
}

void ASIC_set_version_mask(uint32_t mask) {
    current_asics->set_version_mask(mask);
}

bool ASIC_set_frequency(float target_frequency) {
    current_asics->set_frequency(target_frequency);
    ASIC_get_asic_job_frequency_ms();
    return true;
}

double ASIC_get_asic_job_frequency_ms()
{
    // default works for all chips
    float asic_job_frequency_ms = 20;

    // use 1/128 for timeout be approximatly equivalent to Antminer SXX hcn setting 
    uint64_t frequency = POWER_MANAGEMENT_MODULE.frequency_value;
    int chain_chip_count = DEVICE_CONFIG.family.asic_count;

    int versions_to_roll =  MINING_MODULE.version_mask>>13;

    //ESP_LOGI(TAG, "ASIC Job Frequency: %llu Hz, Chain Chip Count: %i, Versions to Roll: %i", frequency, chain_chip_count, versions_to_roll);
    current_asics->set_nonce_percent(frequency, chain_chip_count);
    asic_job_frequency_ms = current_asics->get_timeout(frequency, chain_chip_count, versions_to_roll);

    // set minimum job frequency 
    if (asic_job_frequency_ms < 20) asic_job_frequency_ms = 20;

    return asic_job_frequency_ms;
}

