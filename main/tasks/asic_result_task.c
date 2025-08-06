#include <lwip/tcpip.h>

#include "system.h"
#include "work_queue.h"
#include <string.h>
#include "esp_log.h"
#include "utils.h"
#include "stratum_task.h"
#include "asic.h"
#include "asic_task_module.h"
#include "esp_timer.h"
#include "pool_module.h"
#include "bm1370.h"
#include "system_module.h"

static const char *TAG = "asic_result";
static long timegone = 1;
static int timecounter = 4;

void ASIC_result_task(void *pvParameters)
{

    while (1)
    {
        //task_result *asic_result = (*GLOBAL_STATE.ASIC_functions.receive_result_fn)(GLOBAL_STATE);
        task_result *asic_result = ASIC_process_work();

        if (asic_result == NULL)
        {
            continue;
        }

        uint8_t job_id = asic_result->job_id;

        if (ASIC_TASK_MODULE.valid_jobs[job_id] == 0)
        {
            ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
            continue;
        }

        bm_job *active_job = ASIC_TASK_MODULE.active_jobs[job_id];
        // check the nonce difficulty
        double nonce_diff = test_nonce_value(active_job, asic_result->nonce, asic_result->rolled_version);

        //log the ASIC response
        ESP_LOGI(TAG, "ID: %s, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %ld.", active_job->jobid, asic_result->rolled_version, asic_result->nonce, nonce_diff, active_job->pool_diff);

        if (nonce_diff >= active_job->pool_diff)
        {
            stratum_submit_share(active_job->jobid,
                active_job->extranonce2,
                active_job->ntime,
                asic_result->nonce,
                asic_result->rolled_version ^ active_job->version);
        }

        long now = esp_timer_get_time();
        float gh_hash = get_hashrate_cnt();
        if (gh_hash > 0)
            gh_hash = (gh_hash /(now - timegone)) * 1000000.f;

        float gh_err = get_hashrate_error_cnt();
        if (gh_err > 0)
            gh_err = (gh_err / (now - timegone)) * 1000000.f;
        SYSTEM_MODULE.hashrate_no_error = gh_hash;
        SYSTEM_MODULE.hashrate_error = gh_err;
        if (timecounter-- == 0) {
            timegone = now;
            reset_counters();
            timecounter = 20;
        }
        SYSTEM_notify_found_nonce(nonce_diff, job_id);
    }
}
