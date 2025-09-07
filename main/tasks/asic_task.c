#include "asic.h"
#include "asic_task_common.h"
#include "bm1370.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "system_module.h"
#include <string.h>

#define TAG "asic_task"

// ASIC task configuration constants
#define NONCE_SPACE 4294967296.0 // 2^32
#define QUEUE_LOW_WATER_MARK 10  // Adjust based on requirements
#define JOB_ARRAY_SIZE 128       // Size of job arrays

// Mining notifications
mining_notify * mining_notification_current = NULL;
mining_notify * mining_notification_new = NULL;

// Active jobs and their start times
bm_job ** active_jobs = NULL;
bm_job * active_job = NULL;

// Time tracking
static long timegone = 1;
static int hashrate_counter = 20;
int (*stratum_submit_share_callback)(char * jobid, char * extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version);
void (*SYSTEM_notify_found_nonce_callback)(double found_diff, uint32_t target);

/**
 * String to hold extra nonces used in mining.
 */
char * extranonce_str;
/**
 * Length of the second extra nonce.
 */
int extranonce_2_len;

/**
 * Version mask for stratum protocol handling.
 */
uint32_t version_mask;
bool new_version_mask = false;

/**
 * Initialize ASIC task resources
 */
void asic_task_init(void)
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

void set_extranonce(char * _extranonce_str, int _extranonce_2_len)
{
    extranonce_str = _extranonce_str;
    extranonce_2_len = _extranonce_2_len;
}

void asic_task_set_version_mask(uint32_t _version_mask)
{
    version_mask = _version_mask;
    new_version_mask = true;
}

/**
 * Set new mining notification
 *
 * @param notification Mining notification
 */
void set_new_mining_notification(mining_notify * notification)
{
    mining_notification_new = notification;
}

/**
 * Update hashrate statistics
 */
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

    if (--hashrate_counter == 0) {
        timegone = current_time;
        reset_counters();
        hashrate_counter = 20;
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
    while (1) {
        // Wait for a notification from stratum_task
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "Processing new work...");

        // Handle version mask updates
        if (new_version_mask) {
            ESP_LOGI(TAG, "Set chip version rolls %i", (int) (version_mask >> 13));
            ASIC_set_version_mask(version_mask);
            new_version_mask = false;
        }

        uint32_t extranonce_2 = 0;

        // Clear the current notification and update to new one
        free_mining_notify(mining_notification_current);
        mining_notification_current = mining_notification_new;
        mining_notification_new = NULL;

        // Validate current notification
        if (!mining_notification_current) {
            ESP_LOGE(TAG, "No mining notification available");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // Clean up active job
        if (active_job) {
            free(active_job->jobid);
            free(active_job->extranonce2);
            free(active_job);
            active_job = NULL;
        }

        // Clear the active jobs array
        for (int i = 0; i < JOB_ARRAY_SIZE; i++) {
            if (active_jobs[i]) {
                active_jobs[i] = NULL;
            }
        }

        // Generate new work and send to ASIC
        active_job = generate_work(mining_notification_current, extranonce_2, mining_notification_current->job_difficulty,
                                   extranonce_str, extranonce_2_len,version_mask);
        if (active_job) {
            ASIC_send_work(active_job, active_jobs);
        } else {
            ESP_LOGE(TAG, "Failed to generate work for mining notification");
        }
    }
}

/**
 * Process ASIC results and update statistics
 */
void ASIC_result_task(void * pvParameters)
{
    while (1) {
        task_result * asic_result = ASIC_process_work(active_jobs);

        if (!asic_result) {
            continue;
        }

        uint8_t job_id = asic_result->job_id;
        bm_job * aj = active_jobs[job_id];

        if (!aj) {
            ESP_LOGW(TAG, "Received result for job ID %d but no active job found", job_id);
            continue;
        }

        process_asic_result(asic_result, aj, job_id, SYSTEM_notify_found_nonce_callback, stratum_submit_share_callback);

        update_hashrate(esp_timer_get_time());
    }
}