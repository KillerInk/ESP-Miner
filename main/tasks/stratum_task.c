/* main/tasks/stratum_task.c */
#include "connect.h"
#include "esp_log.h"
#include "system.h"

#include "device_config.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/dns.h"
#include "nvs_config.h"
#include "pool_module.h"
#include "stratum_task.h"
#include "stratum_task_common.h"
#include "system_module.h"
#include "utils.h"

#include <esp_sntp.h>
#include <lwip/tcpip.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>

#define MAX_RETRY_ATTEMPTS 2
#define MAX_EXTRANONCE_2_LEN 32
#define BUFFER_SIZE 1024

static const char *TAG = "stratum_task";

typedef enum {
    STRATUM_STATE_IDLE,
    STRATUM_STATE_CONNECTING,
    STRATUM_STATE_PROCESS_SHARES,
    STRATUM_STATE_ERROR_RETRY
} StratumState;

static StratumState current_state = STRATUM_STATE_IDLE;

/* Function prototypes */
void do_connect(PoolInfo * pool);
void handle_process_shares(PoolInfo * pool);
void handle_retry_or_fallback(void);

void (*set_new_mining_notification_callback)(mining_notify *notify);
void (*set_extranonce_callback)(char *extranonce_str, int extranonce_2_len);
void (*set_version_mask_callback)(uint32_t _version_mask);

int send_uid;

TaskHandle_t create_jobs_task_handle;
int authorize_message_id;

/* Reset share statistics and retry counters after a failover */
static void reset_state_for_retry(void)
{
    for (int i = 0; i < SYSTEM_MODULE.rejected_reason_stats_count; ++i) {
        SYSTEM_MODULE.rejected_reason_stats[i].count = 0;
        SYSTEM_MODULE.rejected_reason_stats[i].message[0] = '\0';
    }
    SYSTEM_MODULE.rejected_reason_stats_count = 0;
    SYSTEM_MODULE.shares_accepted = 0;
    SYSTEM_MODULE.shares_rejected = 0;
    SYSTEM_MODULE.work_received = 0;
}

/* Simple wrapper to avoid repetitive assignments */
static inline void set_next_state(StratumState state)
{
    current_state = state;
}

/* Submit a share with the active pool socket */
int stratum_submit_share(char *jobid, char *extranonce2, uint32_t ntime,
                          uint32_t nonce, uint32_t version)
{
    return stratum_submit_share_(
        jobid, extranonce2, ntime, nonce, version,
        &POOL_MODULE.pools[POOL_MODULE.active_pool].sock,
        send_uid++);
}

/* Helper for deciding whether to switch pools */
static inline bool attempt_pool_switch(void)
{
    POOL_MODULE.active_pool = (POOL_MODULE.active_pool != POOL_MAIN) ? POOL_MAIN : POOL_FALLBACK;
    reset_state_for_retry();
    ESP_LOGI(TAG, "Switched active pool to %s",
             (POOL_MODULE.active_pool == POOL_MAIN) ? "MAIN" : "FALLBACK");
    return true;
}

/* Handle retry or fallback logic */
void handle_retry_or_fallback(void)
{
    if (!attempt_pool_switch()) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    set_next_state(STRATUM_STATE_CONNECTING);
}

/* Heartbeat thread for the primary pool */
void stratum_primary_heartbeat(void *pvParameters)
{

    ESP_LOGI(TAG, "Starting heartbeat thread for primary pool: %s:%d",
             POOL_MODULE.pools[POOL_MAIN].url, POOL_MODULE.pools[POOL_MAIN].port);
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1) {
        if (!POOL_MODULE.active_pool) { /* 0 == POOL_MAIN */
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!connect_to_stratum_server(POOL_MODULE.pools[POOL_MAIN].url, POOL_MODULE.pools[POOL_MAIN].port,
                                       &POOL_MODULE.pools[POOL_MAIN].sock))
            continue;


        int send_uid_local = 1;
        STRATUM_V1_subscribe(POOL_MODULE.pools[POOL_MAIN].sock, send_uid_local++, DEVICE_CONFIG.family.asic.name);
        STRATUM_V1_authorize(POOL_MODULE.pools[POOL_MAIN].sock, send_uid_local++, POOL_MODULE.pools[POOL_MAIN].user,
                             POOL_MODULE.pools[POOL_MAIN].pass);

        char recv_buffer[BUFFER_SIZE];
        memset(recv_buffer, 0, sizeof(recv_buffer));
        int bytes_received = recv(POOL_MODULE.pools[POOL_MAIN].sock, recv_buffer, BUFFER_SIZE - 1, 0);

        bool hb_ok = (bytes_received != -1 && strstr(recv_buffer, "mining.notify") != NULL);
        if (hb_ok) {
            //handle_retry_or_fallback();
            ESP_LOGI(TAG, "Primary Pool is back online");
            stratum_close_connection(&POOL_MODULE.pools[POOL_MAIN].sock);
            stratum_close_connection(&POOL_MODULE.pools[POOL_FALLBACK].sock);
            vTaskDelay(pdMS_TO_TICKS(5000));
            //set_next_state(STRATUM_STATE_ERROR_RETRY); /* reconnect to primary */
        } else {
            shutdown(POOL_MODULE.pools[POOL_MAIN].sock, SHUT_RDWR);
            close(POOL_MODULE.pools[POOL_MAIN].sock);
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

#define HEARTBEAT_TASK_STACK_SIZE  8192
static StaticTask_t heartbeat_task_buffer;
static StackType_t heartbeat_task_stack[HEARTBEAT_TASK_STACK_SIZE];

/* Main Stratum task loop */
void stratum_task(void *pvParameters)
{
    PoolInfo * pool = &POOL_MODULE.pools[POOL_MODULE.active_pool];
    /* Pull current pool configuration once at start */

    STRATUM_V1_initialize_buffer();

    create_jobs_task_handle = xTaskGetHandle("stratum miner");
    xTaskCreateStatic(stratum_primary_heartbeat, "stratum primary heartbeat", HEARTBEAT_TASK_STACK_SIZE,
                pvParameters, 1, heartbeat_task_stack, &heartbeat_task_buffer);

    ESP_LOGI(TAG, "Stratum task started");

    for (;;) {
        pool = &POOL_MODULE.pools[POOL_MODULE.active_pool];
        switch (current_state) {

        case STRATUM_STATE_IDLE:
            if (!is_wifi_connected()) {
                vTaskDelay(pdMS_TO_TICKS(10000));
                break; /* stay idle */
            }
            /* fall through to CONNECTING */

        case STRATUM_STATE_CONNECTING:
            do_connect(pool);
            break;

        case STRATUM_STATE_PROCESS_SHARES:
            handle_process_shares(pool); /* unchanged logic */
            break;

        case STRATUM_STATE_ERROR_RETRY:
            handle_retry_or_fallback();
            break;
        }
    }
}

/* Updated connect logic to use the new helper */
void do_connect(PoolInfo * pool)
{
    char *stratum_url = pool->url;
    uint16_t port = pool->port;

    if (!connect_to_stratum_server(stratum_url, port,
                                   &pool->sock)) {
        handle_retry_or_fallback();
        return;
    }

    send_uid = send_initial_messages(authorize_message_id,
                                     &pool->sock,
                                     pool->stratum_api_v1_message.version_mask);
    set_next_state(STRATUM_STATE_PROCESS_SHARES);
}

/* Handle processing of shares */
void handle_process_shares(PoolInfo * pool)
{
    /* Process a single JSON‑RPC line from the socket. */
    char *line = STRATUM_V1_receive_jsonrpc_line(
        pool->sock);
    if (!line) {
        ESP_LOGE(TAG, "Failed to receive JSON-RPC line, reconnecting...");
        stratum_close_connection(&pool->sock);
        set_next_state(STRATUM_STATE_ERROR_RETRY);
        return;
    }

    double response_time_ms = STRATUM_V1_get_response_time_ms(
        pool->stratum_api_v1_message.message_id);
    if (response_time_ms >= 0) {
        ESP_LOGI(TAG, "Stratum response time: %.2f ms", response_time_ms);
        POOL_MODULE.response_time = response_time_ms;
    }

    STRATUM_V1_parse(&pool->stratum_api_v1_message, line);
    free(line);

    /* Handle parsed message immediately */
    switch (pool->stratum_api_v1_message.method) {
    case MINING_NOTIFY:
        SYSTEM_notify_new_ntime(pool->stratum_api_v1_message.mining_notification->ntime);
        pool->stratum_api_v1_message.mining_notification->job_difficulty =
            pool->difficulty;
        set_new_mining_notification_callback(
            pool->stratum_api_v1_message.mining_notification);

        /*if (create_jobs_task_handle != NULL) {
            xTaskNotifyGive(create_jobs_task_handle);
        }*/
        decode_mining_notification(pool->stratum_api_v1_message.mining_notification,
                                   pool->stratum_api_v1_message.extranonce_str,
                                   pool->stratum_api_v1_message.extranonce_2_len);
        break;

    case MINING_SET_DIFFICULTY:
        ESP_LOGI(TAG, "Set pool difficulty: %ld",
                 pool->stratum_api_v1_message.new_difficulty);
        pool->difficulty =
            pool->stratum_api_v1_message.new_difficulty;
        break;

    case MINING_SET_VERSION_MASK:
    case STRATUM_RESULT_VERSION_MASK:
        ESP_LOGI(TAG, "Set version mask: %08lx",
                 pool->stratum_api_v1_message.version_mask);
        set_version_mask_callback(pool->stratum_api_v1_message.version_mask);
        break;

    case MINING_SET_EXTRANONCE:
    case STRATUM_RESULT_SUBSCRIBE:
        if (pool->stratum_api_v1_message.extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
            pool->stratum_api_v1_message.extranonce_2_len = MAX_EXTRANONCE_2_LEN;
        }
        ESP_LOGI(TAG, "Set extranonce: %s, extranonce_2_len: %d",
                 pool->stratum_api_v1_message.extranonce_str,
                 pool->stratum_api_v1_message.extranonce_2_len);
        set_extranonce_callback(pool->stratum_api_v1_message.extranonce_str,
                                pool->stratum_api_v1_message.extranonce_2_len);
        break;

    case CLIENT_RECONNECT:
        ESP_LOGE(TAG, "Pool requested client reconnect...");
        stratum_close_connection(&pool->sock);
        set_next_state(STRATUM_STATE_ERROR_RETRY);
        return;

    case STRATUM_RESULT:
        if (pool->stratum_api_v1_message.response_success) {
            ESP_LOGI(TAG, "message result accepted");
            SYSTEM_notify_accepted_share();
        } else {
            ESP_LOGW(TAG, "message result rejected: %s",
                     pool->stratum_api_v1_message.error_str);
            SYSTEM_notify_rejected_share(pool->stratum_api_v1_message.error_str);
        }
        break;

    case STRATUM_RESULT_SETUP:
        if (pool->stratum_api_v1_message.response_success) {
            ESP_LOGI(TAG, "setup message accepted");
            if (pool->stratum_api_v1_message.message_id == authorize_message_id) {
                STRATUM_V1_suggest_difficulty(
                    pool->sock,
                    send_uid++, pool->difficulty);
            }
            if (pool->extranonce_subscribe) {
                STRATUM_V1_extranonce_subscribe(
                    pool->sock,
                    send_uid++);
            }
        } else {
            ESP_LOGE(TAG, "setup message rejected: %s",
                     pool->stratum_api_v1_message.error_str);
        }
        break;

    default:
        /* Unknown or unhandled method – ignore */
        break;
    }

    /* Remain in the same state to continue processing further shares. */
}