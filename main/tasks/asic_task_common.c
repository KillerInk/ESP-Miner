#include "asic_task_common.h"
#include "esp_log.h"
#include "mining_module.h"
#define TAG "asic_task_common"

/**
 * Process ASIC result and update statistics
 */
void process_asic_result(task_result * asic_result, 
    bm_job * active_job, 
    uint8_t job_id,
    void (*SYSTEM_notify_found_nonce_callback)(double found_diff, uint32_t target),
    int (*stratum_submit_share_callback)(char * jobid, char * extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version))
{
    // Check the nonce difficulty
    double nonce_diff = test_nonce_value(active_job, asic_result->nonce, asic_result->rolled_version);

    // Log the ASIC response
    ESP_LOGI(TAG, "ID: %s, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %ld.", 
             active_job->jobid,
             asic_result->rolled_version, 
             asic_result->nonce, 
             nonce_diff, 
             active_job->pool_diff);

    if (nonce_diff >= active_job->pool_diff) {
        stratum_submit_share_callback(active_job->jobid, active_job->extranonce2, active_job->ntime, 
                             asic_result->nonce, 
                             asic_result->rolled_version ^ active_job->version);
    }
    SYSTEM_notify_found_nonce_callback(nonce_diff, active_job->target);
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
bm_job * generate_work(mining_notify * notification, uint32_t extranonce_2, uint32_t difficulty)
{
    char * extranonce_2_str = extranonce_2_generate(extranonce_2, MINING_MODULE.extranonce_2_len);
    if (!extranonce_2_str) {
        ESP_LOGE(TAG, "Failed to generate extranonce_2");
        return NULL;
    }

    char * coinbase_tx = construct_coinbase_tx(notification->coinbase_1, notification->coinbase_2, 
                                               MINING_MODULE.extranonce_str, extranonce_2_str);
    if (!coinbase_tx) {
        ESP_LOGE(TAG, "Failed to construct coinbase_tx");
        free(extranonce_2_str);
        return NULL;
    }

    char * merkle_root = calculate_merkle_root_hash(coinbase_tx, (uint8_t (*)[32]) notification->merkle_branches, 
                                                  notification->n_merkle_branches);
    if (!merkle_root) {
        ESP_LOGE(TAG, "Failed to calculate merkle_root");
        free(extranonce_2_str);
        free(coinbase_tx);
        return NULL;
    }

    bm_job next_job = construct_bm_job(notification, merkle_root, MINING_MODULE.version_mask, difficulty);

    bm_job * queued_next_job = malloc(sizeof(bm_job));
    if (!queued_next_job) {
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