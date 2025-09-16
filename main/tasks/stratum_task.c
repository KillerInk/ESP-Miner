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
#include "system_module.h"
#include "utils.h"
#include <esp_sntp.h>
#include <lwip/tcpip.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>

#define MAX_RETRY_ATTEMPTS      3
#define MAX_CRITICAL_RETRY_ATTEMPTS 5
#define MAX_EXTRANONCE_2_LEN   32
#define BUFFER_SIZE            1024

static const char * TAG = "stratum_task";


typedef enum {
    STRATUM_STATE_IDLE,
    STRATUM_STATE_CONNECTING,
    STRATUM_STATE_AUTHENTICATING,
    STRATUM_STATE_WAIT_NOTIFY,
    STRATUM_STATE_PROCESS_SHARES,
    STRATUM_STATE_ERROR_RETRY,
    STRATUM_STATE_SWITCH_FALLBACK,
    STRATUM_STATE_SHUTDOWN
} StratumState;

static StratumState current_state = STRATUM_STATE_IDLE;

/* Forward declarations of state handlers */
 void handle_idle(void);
 void handle_connecting(void);
 void handle_authenticating(void);
 void handle_wait_notify(void);
 void handle_process_shares(void);
 void handle_error_retry(void);
 void handle_switch_fallback(void);
 void handle_shutdown(void);

/* Queue used by the heartbeat thread to signal success */
static QueueHandle_t g_heartbeat_q;

static StratumApiV1Message stratum_api_v1_message = {};
static char * primary_stratum_url;
static uint16_t primary_stratum_port;

void (*set_new_mining_notification_callback)(mining_notify * notify);
void (*set_extranonce_callback)(char * extranonce_str, int extranonce_2_len);
void (*set_version_mask_callback)(uint32_t _version_mask);

int sock;
int send_uid;
bool extranonce_subscribe;
uint16_t difficulty;

int retry_critical_attempts = 0;
int retry_attempts = 0;
TaskHandle_t create_jobs_task_handle;
int authorize_message_id;
char host_ip[20];

struct timeval tcp_snd_timeout = {.tv_sec = 5, .tv_usec = 0};
struct timeval tcp_rcv_timeout = {.tv_sec = 60 * 10, .tv_usec = 0};

bool is_wifi_connected()
{
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

void stratum_reset_uid()
{
    ESP_LOGI(TAG, "Resetting stratum uid");
    send_uid = 1;
}

void stratum_close_connection()
{
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket already shutdown, not shutting down again..");
        return;
    }

    ESP_LOGE(TAG, "Shutting down socket and restarting...");
    shutdown(sock, SHUT_RDWR);
    close(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

int stratum_submit_share(char * jobid, char * extranonce2,
                         uint32_t ntime, uint32_t nonce, uint32_t version)
{
    char * user = POOL_MODULE.is_using_fallback ? POOL_MODULE.fallback_pool_user : POOL_MODULE.pool_user;
    int ret = STRATUM_V1_submit_share(sock, send_uid++, user,
                                      jobid, extranonce2, ntime, nonce, version);

    if (ret < 0) {
        ESP_LOGI(TAG, "Unable to write share to socket. Closing connection. Ret: %d (errno %d: %s)",
                 ret, errno, strerror(errno));
        stratum_close_connection();
    }
    return ret;
}

void decode_mining_notification(const mining_notify * mining_notification)
{
    double network_difficulty = networkDifficulty(mining_notification->target);
    suffixString(network_difficulty,
                 SYSTEM_MODULE.network_diff_string,
                 DIFF_STRING_SIZE, 0);

    int coinbase_1_len = strlen(mining_notification->coinbase_1) / 2;
    int coinbase_2_len = strlen(mining_notification->coinbase_2) / 2;

    int coinbase_1_offset = 41; // Skip version (4), inputcount (1),
                                // prevhash (32), vout (4)
    if (coinbase_1_len < coinbase_1_offset)
        return;

    uint8_t scriptsig_len;
    hex2bin(mining_notification->coinbase_1 + (coinbase_1_offset * 2),
            &scriptsig_len, 1);
    coinbase_1_offset++;

    if (coinbase_1_len < coinbase_1_offset)
        return;

    uint8_t block_height_len;
    hex2bin(mining_notification->coinbase_1 + (coinbase_1_offset * 2),
            &block_height_len, 1);
    coinbase_1_offset++;

    if (coinbase_1_len < coinbase_1_offset ||
        block_height_len == 0 || block_height_len > 4)
        return;

    uint32_t block_height = 0;
    hex2bin(mining_notification->coinbase_1 + (coinbase_1_offset * 2),
            (uint8_t *) &block_height, block_height_len);
    coinbase_1_offset += block_height_len;

    if (block_height != SYSTEM_MODULE.block_height) {
        ESP_LOGI(TAG, "Block height %d", block_height);
        SYSTEM_MODULE.block_height = block_height;
    }

    size_t scriptsig_length =
            scriptsig_len - 1 - block_height_len -
            (strlen(stratum_api_v1_message.extranonce_str) / 2) -
            stratum_api_v1_message.extranonce_2_len;
    if (scriptsig_length <= 0)
        return;

    char * scriptsig = malloc(scriptsig_length + 1);

    int coinbase_1_tag_len = coinbase_1_len - coinbase_1_offset;
    hex2bin(mining_notification->coinbase_1 +
            (coinbase_1_offset * 2),
            (uint8_t *) scriptsig, coinbase_1_tag_len);

    int coinbase_2_tag_len = scriptsig_length - coinbase_1_tag_len;

    if (coinbase_2_len < coinbase_2_tag_len)
        return;

    if (coinbase_2_tag_len > 0) {
        hex2bin(mining_notification->coinbase_2,
                (uint8_t *) scriptsig + coinbase_1_tag_len,
                coinbase_2_tag_len);
    }

    for (int i = 0; i < scriptsig_length; i++) {
        if (!isprint((unsigned char) scriptsig[i])) {
            scriptsig[i] = '.';
        }
    }

    scriptsig[scriptsig_length] = '\0';

    if (SYSTEM_MODULE.scriptsig == NULL ||
        strcmp(scriptsig, SYSTEM_MODULE.scriptsig) != 0) {
        ESP_LOGI(TAG, "Scriptsig: %s", scriptsig);

        char * previous_miner_tag = SYSTEM_MODULE.scriptsig;
        SYSTEM_MODULE.scriptsig = scriptsig;
        free(previous_miner_tag);
    } else {
        free(scriptsig);
    }
}

bool switch_to_fallback_pool()
{
    if (retry_attempts >= MAX_RETRY_ATTEMPTS) {
        if (POOL_MODULE.fallback_pool_url == NULL ||
            POOL_MODULE.fallback_pool_url[0] == '\0') {
            ESP_LOGI(TAG,
                     "Unable to switch to fallback. No url configured. "
                     "(retries: %d)...",
                     retry_attempts);
            POOL_MODULE.is_using_fallback = false;
            retry_attempts = 0;
            return false;
        }

        POOL_MODULE.is_using_fallback = !POOL_MODULE.is_using_fallback;

        /* Reset share stats at failover */
        for (int i = 0; i < SYSTEM_MODULE.rejected_reason_stats_count; i++) {
            SYSTEM_MODULE.rejected_reason_stats[i].count = 0;
            SYSTEM_MODULE.rejected_reason_stats[i].message[0] = '\0';
        }
        SYSTEM_MODULE.rejected_reason_stats_count = 0;
        SYSTEM_MODULE.shares_accepted = 0;
        SYSTEM_MODULE.shares_rejected = 0;
        SYSTEM_MODULE.work_received = 0;

        ESP_LOGI(TAG,
                 "Switching target due to too many failures "
                 "(retries: %d)...",
                 retry_attempts);
        retry_attempts = 0;
    }
    return true;
}

bool connect_to_stratum_server(char * stratum_url, uint16_t port)
{
    struct hostent * dns_addr = gethostbyname(stratum_url);
    if (dns_addr == NULL) {
        retry_attempts++;
        vTaskDelay(pdMS_TO_TICKS(1000));
        return false;
    }
    inet_ntop(AF_INET,
              (void *) dns_addr->h_addr_list[0],
              host_ip, sizeof(host_ip));

    ESP_LOGI(TAG,
             "Connecting to: stratum+tcp://%s:%d (%s)",
             stratum_url, port, host_ip);

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(host_ip);
    dest_addr.sin_family      = AF_INET;
    dest_addr.sin_port        = htons(port);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    vTaskDelay(pdMS_TO_TICKS(300));
    if (sock < 0) {
        ESP_LOGE(TAG,
                 "Unable to create socket: errno %d",
                 errno);
        if (++retry_critical_attempts > MAX_CRITICAL_RETRY_ATTEMPTS) {
            ESP_LOGE(TAG,
                     "Max retry attempts reached, restarting...");
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
        return false;
    }
    retry_critical_attempts = 0;

    ESP_LOGI(TAG,
             "Socket created, connecting to %s:%d",
             host_ip, port);
    int err = connect(sock,
                      (struct sockaddr *) &dest_addr,
                      sizeof(struct sockaddr_in));
    if (err != 0) {
        retry_attempts++;
        ESP_LOGE(TAG,
                 "Socket unable to connect to %s:%d "
                 "(errno %d: %s)",
                 stratum_url, port,
                 errno, strerror(errno));
        shutdown(sock, SHUT_RDWR);
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(5000));
        return false;
    }

    if (setsockopt(sock,
                   SOL_SOCKET,
                   SO_SNDTIMEO,
                   &tcp_snd_timeout,
                   sizeof(tcp_snd_timeout)) != 0) {
        ESP_LOGE(TAG, "Fail to setsockopt SO_SNDTIMEO");
    }

    if (setsockopt(sock,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   &tcp_rcv_timeout,
                   sizeof(tcp_rcv_timeout)) != 0) {
        ESP_LOGE(TAG, "Fail to setsockopt SO_RCVTIMEO ");
    }
    return true;
}

void send_initial_messages()
{
    stratum_reset_uid();
    STRATUM_V1_configure_version_rolling(sock,
                                         send_uid++,
                                         &stratum_api_v1_message.version_mask);
    STRATUM_V1_subscribe(sock, send_uid++, DEVICE_CONFIG.family.asic.name);

    char * username = POOL_MODULE.is_using_fallback ?
                      POOL_MODULE.fallback_pool_user : POOL_MODULE.pool_user;
    char * password = POOL_MODULE.is_using_fallback ?
                      POOL_MODULE.fallback_pool_pass : POOL_MODULE.pool_pass;

    authorize_message_id = send_uid++;
    STRATUM_V1_authorize(sock,
                         authorize_message_id,
                         username, password);
    STRATUM_V1_stamp_tx(authorize_message_id);
}

void stratum_primary_heartbeat(void * pvParameters)
{
    ESP_LOGI(TAG,
             "Starting heartbeat thread for primary pool: %s:%d",
             primary_stratum_url, primary_stratum_port);
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1) {
        if (!POOL_MODULE.is_using_fallback) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!connect_to_stratum_server(primary_stratum_url,
                                       primary_stratum_port))
            continue;

        int send_uid = 1;
        STRATUM_V1_subscribe(sock, send_uid++, DEVICE_CONFIG.family.asic.name);
        STRATUM_V1_authorize(sock, send_uid++,
                             POOL_MODULE.pool_user,
                             POOL_MODULE.pool_pass);

        char recv_buffer[BUFFER_SIZE];
        memset(recv_buffer, 0, sizeof(recv_buffer));
        int bytes_received = recv(sock,
                                  recv_buffer,
                                  BUFFER_SIZE - 1,
                                  0);

        shutdown(sock, SHUT_RDWR);
        close(sock);

        bool hb_ok = (bytes_received != -1 &&
                      strstr(recv_buffer, "mining.notify") != NULL);
        xQueueSend(g_heartbeat_q, &hb_ok, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

void stratum_task(void * pvParameters)
{
    primary_stratum_url = POOL_MODULE.pool_url;
    primary_stratum_port = POOL_MODULE.pool_port;
    char * stratum_url = POOL_MODULE.is_using_fallback ? POOL_MODULE.fallback_pool_url : POOL_MODULE.pool_url;
    uint16_t port = POOL_MODULE.is_using_fallback ? POOL_MODULE.fallback_pool_port : POOL_MODULE.pool_port;
    extranonce_subscribe = POOL_MODULE.is_using_fallback ? POOL_MODULE.fallback_pool_extranonce_subscribe : POOL_MODULE.pool_extranonce_subscribe;
    difficulty =  POOL_MODULE.is_using_fallback ? POOL_MODULE.fallback_pool_difficulty : POOL_MODULE.pool_difficulty;

    STRATUM_V1_initialize_buffer();
    
    create_jobs_task_handle = xTaskGetHandle("stratum miner");
    xTaskCreate(stratum_primary_heartbeat, "stratum primary heartbeat", 8192, pvParameters, 1, NULL);

    /* Create queue for heartbeat notifications */
    g_heartbeat_q = xQueueCreate(1, sizeof(bool));

    ESP_LOGI(TAG, "Stratum task started");

    /* Main state‑machine loop */
    for (;;) {
        switch (current_state) {
            case STRATUM_STATE_IDLE:          handle_idle(); break;
            case STRATUM_STATE_CONNECTING:    handle_connecting(); break;
            case STRATUM_STATE_AUTHENTICATING:handle_authenticating(); break;
            case STRATUM_STATE_WAIT_NOTIFY:   handle_wait_notify(); break;
            case STRATUM_STATE_PROCESS_SHARES:handle_process_shares(); break;
            case STRATUM_STATE_ERROR_RETRY:   handle_error_retry(); break;
            case STRATUM_STATE_SWITCH_FALLBACK:handle_switch_fallback(); break;
            case STRATUM_STATE_SHUTDOWN:      handle_shutdown(); break;
        }
    }
}

 void handle_idle(void)
{
    if (!is_wifi_connected()) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        return;
    }
    current_state = STRATUM_STATE_CONNECTING;
}

 void handle_connecting(void)
{
    /* Resolve the URL that is currently active (primary or fallback) */
    char *stratum_url  = POOL_MODULE.is_using_fallback ?
                         POOL_MODULE.fallback_pool_url : POOL_MODULE.pool_url;
    uint16_t port = POOL_MODULE.is_using_fallback ?
                    POOL_MODULE.fallback_pool_port : POOL_MODULE.pool_port;

    if (!connect_to_stratum_server(stratum_url, port)) {
        current_state = STRATUM_STATE_ERROR_RETRY;
        return;
    }

    /* Connection succeeded – move to authentication */
    current_state = STRATUM_STATE_AUTHENTICATING;
}

 void handle_authenticating(void)
{
    send_initial_messages();
    current_state = STRATUM_STATE_WAIT_NOTIFY;
}

 void handle_wait_notify(void)
{
    /* Heartbeat may have switched us back to primary; consume the queue */
    bool hb_ok = false;
    if (xQueueReceive(g_heartbeat_q, &hb_ok, 0) && hb_ok) {
        current_state = STRATUM_STATE_CONNECTING;
        return;
    }

    char *line = STRATUM_V1_receive_jsonrpc_line(sock);
    if (!line) {
        ESP_LOGE(TAG, "Failed to receive JSON-RPC line, reconnecting...");
        retry_attempts++;
        stratum_close_connection();
        current_state = STRATUM_STATE_ERROR_RETRY;
        return;
    }

    double response_time_ms = STRATUM_V1_get_response_time_ms(stratum_api_v1_message.message_id);
    if (response_time_ms >= 0) {
        ESP_LOGI(TAG, "Stratum response time: %.2f ms", response_time_ms);
        POOL_MODULE.response_time = response_time_ms;
    }
    current_state = STRATUM_STATE_PROCESS_SHARES;
}

void handle_process_shares(void)
{
    /* Process a single JSON‑RPC line from the socket. */
    char *line = STRATUM_V1_receive_jsonrpc_line(sock);
    if (!line) {
        ESP_LOGE(TAG, "Failed to receive JSON-RPC line, reconnecting...");
        retry_attempts++;
        stratum_close_connection();
        current_state = STRATUM_STATE_ERROR_RETRY;
        return;
    }

    double response_time_ms = STRATUM_V1_get_response_time_ms(stratum_api_v1_message.message_id);
    if (response_time_ms >= 0) {
        ESP_LOGI(TAG, "Stratum response time: %.2f ms", response_time_ms);
        POOL_MODULE.response_time = response_time_ms;
    }

    STRATUM_V1_parse(&stratum_api_v1_message, line);
    free(line);

    /* Handle parsed message immediately */
    switch (stratum_api_v1_message.method) {
        case MINING_NOTIFY:
            SYSTEM_notify_new_ntime(stratum_api_v1_message.mining_notification->ntime);
            stratum_api_v1_message.mining_notification->job_difficulty = POOL_MODULE.pool_difficulty;
            set_new_mining_notification_callback(stratum_api_v1_message.mining_notification);

            if (create_jobs_task_handle != NULL) {
                xTaskNotifyGive(create_jobs_task_handle);
            }
            decode_mining_notification(stratum_api_v1_message.mining_notification);
            break;

        case MINING_SET_DIFFICULTY:
            ESP_LOGI(TAG, "Set pool difficulty: %ld", stratum_api_v1_message.new_difficulty);
            POOL_MODULE.pool_difficulty = stratum_api_v1_message.new_difficulty;
            break;

        case MINING_SET_VERSION_MASK:
        case STRATUM_RESULT_VERSION_MASK:
            ESP_LOGI(TAG, "Set version mask: %08lx", stratum_api_v1_message.version_mask);
            set_version_mask_callback(stratum_api_v1_message.version_mask);
            break;

        case MINING_SET_EXTRANONCE:
        case STRATUM_RESULT_SUBSCRIBE:
            if (stratum_api_v1_message.extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
                stratum_api_v1_message.extranonce_2_len = MAX_EXTRANONCE_2_LEN;
            }
            ESP_LOGI(TAG, "Set extranonce: %s, extranonce_2_len: %d",
                     stratum_api_v1_message.extranonce_str,
                     stratum_api_v1_message.extranonce_2_len);
            set_extranonce_callback(stratum_api_v1_message.extranonce_str,
                                    stratum_api_v1_message.extranonce_2_len);
            break;

        case CLIENT_RECONNECT:
            ESP_LOGE(TAG, "Pool requested client reconnect...");
            stratum_close_connection();
            current_state = STRATUM_STATE_ERROR_RETRY;
            return;

        case STRATUM_RESULT:
            if (stratum_api_v1_message.response_success) {
                ESP_LOGI(TAG, "message result accepted");
                SYSTEM_notify_accepted_share();
            } else {
                ESP_LOGW(TAG, "message result rejected: %s",
                         stratum_api_v1_message.error_str);
                SYSTEM_notify_rejected_share(stratum_api_v1_message.error_str);
            }
            break;

        case STRATUM_RESULT_SETUP:
            retry_attempts = 0;
            if (stratum_api_v1_message.response_success) {
                ESP_LOGI(TAG, "setup message accepted");
                if (stratum_api_v1_message.message_id == authorize_message_id) {
                    STRATUM_V1_suggest_difficulty(sock, send_uid++, difficulty);
                }
                if (extranonce_subscribe) {
                    STRATUM_V1_extranonce_subscribe(sock, send_uid++);
                }
            } else {
                ESP_LOGE(TAG, "setup message rejected: %s",
                         stratum_api_v1_message.error_str);
            }
            break;

        default:
            /* Unknown or unhandled method – ignore */
            break;
    }

    /* Remain in the same state to continue processing further shares. */
}

 void handle_error_retry(void)
{
    /* Retry logic – if we hit max attempts we switch to fallback */
    if (!switch_to_fallback_pool()) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        current_state = STRATUM_STATE_CONNECTING;
        return;
    }

    /* If we switched, reconnect using the new pool URL */
    current_state = STRATUM_STATE_CONNECTING;
}

 void handle_switch_fallback(void)
{
    /* The state machine will automatically move to CONNECTING next tick. */
    current_state = STRATUM_STATE_CONNECTING;
}

 void handle_shutdown(void)
{
    stratum_close_connection();
    esp_restart();
}
