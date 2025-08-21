#include "asic.h"
#include "bm1370.h"
#include "device_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "mining_module.h"
#include "pool_module.h"
#include "power_management_module.h"
#include "stratum_task.h"
#include "system.h"
#include "system_module.h"
#include <string.h>
#include "asic.h"

#define TAG "asic_task"

// ASIC task configuration constants
#define NONCE_SPACE 4294967296.0 // 2^32
#define QUEUE_LOW_WATER_MARK 10  // Adjust based on requirements
#define JOB_ARRAY_SIZE 128       // Size of job arrays

// Mining notifications
mining_notify * mining_notification_current;
mining_notify * mining_notification_new;

// Active jobs and their start times
bm_job ** active_jobs;
bm_job *active_job;

long timegone = 1;


/**
 * Initialize ASIC task resources
 */
void asic_task_init()
{
    active_jobs = malloc(sizeof(bm_job *) * JOB_ARRAY_SIZE);
    if (!active_jobs) {
        ESP_LOGE(TAG, "Failed to allocate memory for active_jobs");
        return;
    }

    // Initialize arrays
    for (int i = 0; i < JOB_ARRAY_SIZE; i++) {
        active_jobs[i] = NULL;
    }
}

/**
 * Set new mining notification with memory copy
 *
 * @param notification Mining notification to copy
 *
 * This function creates a deep copy of the mining notification, including all
 * strings and merkle branches. The original notification can then be freed
 * by the caller. If allocation fails, the function logs an error.
 */
void set_new_mining_notification(mining_notify * notification)
{
    // Validate notification parameter
    if (notification == NULL) {
        ESP_LOGE(TAG, "NULL mining notification provided");
        return;
    }

    if (notification != NULL) {
        // Make a copy of the notification since it will be freed by the caller
        char * job_id = strdup(notification->job_id);
        char * prev_block_hash = strdup(notification->prev_block_hash);
        char * coinbase_1 = strdup(notification->coinbase_1);
        char * coinbase_2 = strdup(notification->coinbase_2);

        mining_notify * copy = malloc(sizeof(mining_notify));
        if (!copy) {
            ESP_LOGE(TAG, "Failed to allocate memory for mining notification copy");
            return;
        }
        memcpy(copy, notification, sizeof(mining_notify));

        // Copy the strings
        copy->job_id = job_id;
        copy->prev_block_hash = prev_block_hash;
        copy->coinbase_1 = coinbase_1;
        copy->coinbase_2 = coinbase_2;

        // Copy the merkle branches
        if (notification->merkle_branches != NULL) {
            copy->merkle_branches = malloc(notification->n_merkle_branches * sizeof(uint8_t[32]));
            if (!copy->merkle_branches) {
                free(copy);
                ESP_LOGE(TAG, "Failed to allocate memory for merkle branches");
                return;
            }
            memcpy(copy->merkle_branches, notification->merkle_branches, notification->n_merkle_branches * sizeof(uint8_t[32]));
        }
        
        mining_notification_new = copy;
    }
}
/**
 * Process ASIC result and update statistics
 */
static void process_asic_result(task_result * asic_result, bm_job * active_job, uint8_t job_id)
{
    // Check the nonce difficulty
    double nonce_diff = test_nonce_value(active_job, asic_result->nonce, asic_result->rolled_version);

    // Log the ASIC response
    ESP_LOGI(TAG, "ID: %s, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %ld.", active_job->jobid,
             asic_result->rolled_version, asic_result->nonce, nonce_diff, active_job->pool_diff);

    if (nonce_diff >= active_job->pool_diff) {
        stratum_submit_share(active_job->jobid, active_job->extranonce2, active_job->ntime, asic_result->nonce,
                             asic_result->rolled_version ^ active_job->version);
    }
    SYSTEM_notify_found_nonce(nonce_diff, active_job->target);
    
}

/**
 * Update hashrate statistics
 */
int counter = 20;
static void update_hashrate(long current_time)
{
    float gh_hash = get_hashrate_cnt();
    if (gh_hash > 0) {
        gh_hash = (gh_hash / (current_time - timegone)) * 1000000.0f;
    }

    float gh_err = get_hashrate_error_cnt();
    if (gh_err > 0) {
        gh_err = (gh_err / (current_time - timegone)) * 1000000.0f;
    }
    SYSTEM_MODULE.hashrate_no_error = gh_hash;
    SYSTEM_MODULE.hashrate_error = gh_err;
    if (counter-- == 0) {
        timegone = current_time;
        reset_counters();
        counter = 20;
    }
}


/**
 * Generate work from mining notification
 *
 * @param notification Mining notification containing job details
 * @param extranonce_2 Extranonce value for the job
 * @param difficulty Job difficulty
 *
 * This function constructs a bm_job from the notification, allocates memory
 * for the job, and enqueues it to the ASIC jobs queue. It handles all necessary
 * memory management and error checking.
 */
static bm_job * generate_work(mining_notify * notification, uint32_t extranonce_2, uint32_t difficulty)
{
    char * extranonce_2_str = extranonce_2_generate(extranonce_2, MINING_MODULE.extranonce_2_len);
    if (extranonce_2_str == NULL) {
        ESP_LOGE(TAG, "Failed to generate extranonce_2");
        return NULL;
    }

    char * coinbase_tx =
        construct_coinbase_tx(notification->coinbase_1, notification->coinbase_2, MINING_MODULE.extranonce_str, extranonce_2_str);
    if (coinbase_tx == NULL) {
        ESP_LOGE(TAG, "Failed to construct coinbase_tx");
        free(extranonce_2_str);
        return NULL;
    }

    char * merkle_root =
        calculate_merkle_root_hash(coinbase_tx, (uint8_t (*)[32]) notification->merkle_branches, notification->n_merkle_branches);
    if (merkle_root == NULL) {
        ESP_LOGE(TAG, "Failed to calculate merkle_root");
        free(extranonce_2_str);
        free(coinbase_tx);
        return NULL;
    }

    bm_job next_job = construct_bm_job(notification, merkle_root, MINING_MODULE.version_mask, difficulty);

    bm_job * queued_next_job = malloc(sizeof(bm_job));
    if (queued_next_job == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for queued_next_job");
        free(extranonce_2_str);
        free(coinbase_tx);
        free(merkle_root);
        return NULL;
    }

    memcpy(queued_next_job, &next_job, sizeof(bm_job));
    queued_next_job->extranonce2 = extranonce_2_str; // Transfer ownership
    queued_next_job->jobid = strdup(notification->job_id);
    queued_next_job->version_mask = MINING_MODULE.version_mask;

    free(coinbase_tx);
    free(merkle_root);
    return queued_next_job;
}

/**
 * Free mining notification resources
 *
 * @param params Mining notification to free
 *
 * This function frees all dynamically allocated memory associated with
 * a mining notification, including strings and merkle branches. If the
 * parameter is NULL, the function does nothing.
 */
void free_mining_notify(mining_notify * params)
{
    if (params) {
        free(params->job_id);
        free(params->prev_block_hash);
        free(params->coinbase_1);
        free(params->coinbase_2);
        free(params->merkle_branches);
        free(params);
    }
}


/**
 * Create jobs task for processing mining notifications
 *
 * @param pvParameters Task parameters (unused)
 *
 * This function:
 * 1. Waits for notifications from stratum_task
 * 2. Processes new work and updates version masks if needed
 * 3. Generates and enqueues jobs based on mining notifications
 * 4. Handles notification cleanup and memory management
 *
 * The task runs indefinitely, processing each notification to create
 * and enqueue the necessary mining jobs.
 */
void create_jobs_task(void * pvParameters)
{
    // Main loop handling
    while (1) {
        // Wait for a notification from stratum_task
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "Processing new work...");

        // Handle version mask updates
        if (MINING_MODULE.new_stratum_version_rolling_msg) {
            ESP_LOGI(TAG, "Set chip version rolls %i", (int) (MINING_MODULE.version_mask >> 13));
            ASIC_set_version_mask(MINING_MODULE.version_mask);
            MINING_MODULE.new_stratum_version_rolling_msg = false;
        }

        uint32_t extranonce_2 = 0;
        ESP_LOGI(TAG, "Clean Jobs: clearing queue");
        free_mining_notify(mining_notification_current);
        mining_notification_current = mining_notification_new;
        mining_notification_new = NULL;

        // Validate current notification
        if (mining_notification_current == NULL) {
            ESP_LOGE(TAG, "No mining notification available");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
        if(active_job != NULL)
        {
            free(active_job->jobid);
            free(active_job->extranonce2);
            free(active_job);
        }
        for (int i = 0; i < JOB_ARRAY_SIZE; i++) {
            active_jobs[i] = NULL;
        }
        active_job = NULL;
       
        active_job = generate_work(mining_notification_current, extranonce_2, mining_notification_current->job_difficulty);
        ASIC_send_work(active_job, active_jobs);
    }
}

/**
 * Process ASIC results and update statistics
 */
void ASIC_result_task(void * pvParameters)
{
    while (1) {
        task_result * asic_result = ASIC_process_work(active_jobs);

        if (asic_result == NULL) {
            continue;
        }

        uint8_t job_id = asic_result->job_id;
        bm_job * aj = active_jobs[job_id];
        if(aj == NULL)
            return;
        process_asic_result(asic_result, aj, job_id);

        // Update hashrate and job frequency
        update_hashrate(esp_timer_get_time());
    }
}