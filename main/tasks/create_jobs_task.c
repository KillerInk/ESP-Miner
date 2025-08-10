#include <limits.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_system.h"

#include "mining.h"
#include "string.h"
#include "work_queue.h"
#include "freertos/FreeRTOS.h"
#include "asic_task_module.h"
#include "asic.h"
#include "mining_module.h"
#include "pool_module.h"
#include "stratum_task.h"
#include "create_jobs_task.h"

static const char * TAG = "create_jobs_task";

#define QUEUE_LOW_WATER_MARK 10 // Adjust based on your requirements

mining_notify * mining_notification_current;
mining_notify * mining_notification_new;

static bool should_generate_more_work();
static void generate_work(mining_notify * notification, uint32_t extranonce_2, uint32_t difficulty);

void create_jobs_task(void * pvParameters)
{
    uint32_t difficulty = POOL_MODULE.pool_difficulty;
    while (1) {
        // Wait for a notification from stratum_task
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

       
        ESP_LOGI(TAG, "Processing new work...");

        if (MINING_MODULE.new_set_mining_difficulty_msg) {
            ESP_LOGI(TAG, "New pool difficulty %i", POOL_MODULE.pool_difficulty);
            difficulty = POOL_MODULE.pool_difficulty;
        }

        if (MINING_MODULE.new_stratum_version_rolling_msg) {
            ESP_LOGI(TAG, "Set chip version rolls %i", (int)(MINING_MODULE.version_mask >> 13));
            ASIC_set_version_mask(MINING_MODULE.version_mask);
            MINING_MODULE.new_stratum_version_rolling_msg = false;
        }

        uint32_t extranonce_2 = 0;
        ESP_LOGI(TAG, "Clean Jobs: clearing queue");
        ASIC_jobs_queue_clear(&MINING_MODULE.ASIC_jobs_queue);
        for (int i = 0; i < 128; i = i + 4) {
            ASIC_TASK_MODULE.valid_jobs[i] = 0;
        }
        mining_notification_current = mining_notification_new;
        mining_notification_new = NULL;
        while (mining_notification_new == NULL) {
            if (should_generate_more_work() && mining_notification_new == NULL) {
                // Get the mining notification from stratum_task
                
                if (mining_notification_current && mining_notification_new == NULL) {
                    generate_work(mining_notification_current, extranonce_2, difficulty);
                    
                    // Increase extranonce_2 for the next job.
                    extranonce_2++;
                } else {
                    ESP_LOGE(TAG, "Failed to get mining notification");
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
            }
            else
            {
                // If no more work needed, wait a bit before checking again.
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        }
        STRATUM_V1_free_mining_notify(mining_notification_current);
    }
}

void set_new_mining_notification(mining_notify * notification)
{
    if (notification != NULL) {
        // Make a copy of the notification since it will be freed by the caller
        char *job_id = strdup(notification->job_id);
        char *prev_block_hash = strdup(notification->prev_block_hash);
        char *coinbase_1 = strdup(notification->coinbase_1);
        char *coinbase_2 = strdup(notification->coinbase_2);

        mining_notify *copy = malloc(sizeof(mining_notify));
        memcpy(copy, notification, sizeof(mining_notify));

        // Copy the strings
        copy->job_id = job_id;
        copy->prev_block_hash = prev_block_hash;
        copy->coinbase_1 = coinbase_1;
        copy->coinbase_2 = coinbase_2;

        // Copy the merkle branches
        if (notification->merkle_branches != NULL) {
            copy->merkle_branches = malloc(notification->n_merkle_branches * sizeof(uint8_t[32]));
            memcpy(copy->merkle_branches, notification->merkle_branches, notification->n_merkle_branches * sizeof(uint8_t[32]));
        }

        mining_notification_new = copy;
    }
}

static bool should_generate_more_work()
{
    return uxQueueMessagesWaiting(MINING_MODULE.ASIC_jobs_queue) < QUEUE_LOW_WATER_MARK;
}

static void generate_work(mining_notify * notification, uint32_t extranonce_2, uint32_t difficulty)
{
    char * extranonce_2_str = extranonce_2_generate(extranonce_2, MINING_MODULE.extranonce_2_len);
    if (extranonce_2_str == NULL) {
        ESP_LOGE(TAG, "Failed to generate extranonce_2");
        return;
    }

    char * coinbase_tx =
        construct_coinbase_tx(notification->coinbase_1, notification->coinbase_2, MINING_MODULE.extranonce_str, extranonce_2_str);
    if (coinbase_tx == NULL) {
        ESP_LOGE(TAG, "Failed to construct coinbase_tx");
        free(extranonce_2_str);
        return;
    }

    char *merkle_root = calculate_merkle_root_hash(coinbase_tx, (uint8_t(*)[32])notification->merkle_branches, notification->n_merkle_branches);
    if (merkle_root == NULL) {
        ESP_LOGE(TAG, "Failed to calculate merkle_root");
        free(extranonce_2_str);
        free(coinbase_tx);
        return;
    }

    bm_job next_job = construct_bm_job(notification, merkle_root, MINING_MODULE.version_mask, difficulty);

    bm_job * queued_next_job = malloc(sizeof(bm_job));
    if (queued_next_job == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for queued_next_job");
        free(extranonce_2_str);
        free(coinbase_tx);
        free(merkle_root);
        return;
    }

    memcpy(queued_next_job, &next_job, sizeof(bm_job));
    queued_next_job->extranonce2 = extranonce_2_str; // Transfer ownership
    queued_next_job->jobid = strdup(notification->job_id);
    queued_next_job->version_mask = MINING_MODULE.version_mask;

    queue_enqueue(&MINING_MODULE.ASIC_jobs_queue, queued_next_job);

    free(coinbase_tx);
    free(merkle_root);
}