#include <lwip/tcpip.h>

#include "asic.h"
#include "bm1370.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_config.h"
#include "serial.h"
#include "stratum_task.h"
#include "system.h"
#include "utils.h"
#include "work_queue.h"
#include <string.h>

static const char * TAG = "asic_result";
static long timegone = 1;
static int timecounter = 4;

void ASIC_result_task(void * pvParameters)
{

    while (1) {
        // task_result *asic_result = (*GLOBAL_STATE.ASIC_functions.receive_result_fn)();
        task_result * asic_result = ASIC_process_work();

        if (asic_result == NULL) {
            continue;
        }

        uint8_t job_id = asic_result->job_id;

        if (GLOBAL_STATE.valid_jobs[job_id] == 0) {
            ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
            continue;
        }

        bm_job * active_job = ASIC_TASK_MODULE.active_jobs[job_id];
        // check the nonce difficulty
        double nonce_diff = test_nonce_value(active_job, asic_result->nonce, asic_result->rolled_version);

        // log the ASIC response

        if (nonce_diff >= active_job->pool_diff) {
            ESP_LOGI(TAG, "ID: %s, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %ld.", active_job->jobid,
                     asic_result->rolled_version, asic_result->nonce, nonce_diff, active_job->pool_diff);
            char * user = SYSTEM_MODULE.is_using_fallback ? SYSTEM_MODULE.fallback_pool_user : SYSTEM_MODULE.pool_user;
            int ret = STRATUM_V1_submit_share(GLOBAL_STATE.sock, GLOBAL_STATE.send_uid++, user, active_job->jobid,
                                              active_job->extranonce2, active_job->ntime, asic_result->nonce,
                                              asic_result->rolled_version ^ active_job->version);

            if (ret < 0) {
                ESP_LOGI(TAG, "Unable to write share to socket. Closing connection. Ret: %d (errno %d: %s)", ret, errno,
                         strerror(errno));
                stratum_close_connection();
            }
            SYSTEM_notify_found_nonce(nonce_diff, job_id);
        }
        if (timecounter-- == 0) {
            float gh_hash = get_hashrate_cnt();
            float gh_err = get_hashrate_error_cnt();
            float gh_tot = gh_hash +gh_err;

            float expected_hashrate_mhs = POWER_MANAGEMENT_MODULE.frequency_value * DEVICE_CONFIG.family.asic.small_core_count *
                                          DEVICE_CONFIG.family.asic.hashrate_test_percentage_target / 1000.0f;
            long now = esp_timer_get_time();
            SYSTEM_MODULE.current_hashrate = 0.8 * SYSTEM_MODULE.current_hashrate  + 0.2 * ((gh_tot / (now - timegone)) * 1000000.f);
            //SYSTEM_MODULE.current_hashrate = ((gh_tot / (now - timegone)) * 1000000.f);
            SYSTEM_MODULE.hashrate_no_error = ((gh_hash / (now - timegone)) * 1000000.f);
            //SYSTEM_MODULE.hashrate_no_error = 0.8 * SYSTEM_MODULE.current_hashrate  + 0.2 * ((gh_hash / (now - timegone)) * 1000000.f);
            SYSTEM_MODULE.hashrate_error = ((gh_err / (now - timegone)) * 1000000.f);
            //SYSTEM_MODULE.hashrate_error = 0.8 * SYSTEM_MODULE.current_hashrate  + 0.2 * ((gh_err / (now - timegone)) * 1000000.f);

            timegone = now;
            reset_counters();
            timecounter = 4;
        }
    }
}
