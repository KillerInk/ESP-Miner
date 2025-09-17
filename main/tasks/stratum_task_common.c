#include "stratum_task_common.h"
#include "device_config.h"
#include "stratum_api.h"
#include "esp_log.h"
#include "pool_module.h"
#include "system_module.h"
#include "utils.h"
#include <ctype.h>
#include <lwip/tcpip.h>
#include "lwip/dns.h"
#include "esp_wifi.h"
#include "socket.h"
#include "netdb.h"

static const char * TAG = "stratum_task_common";

int send_initial_messages(int authorize_message_id, int * sock, uint32_t version_mask)
{
    int send_uid = 1;
    STRATUM_V1_configure_version_rolling(*sock,
                                         send_uid++,
                                         &version_mask);
    STRATUM_V1_subscribe(*sock, send_uid++, DEVICE_CONFIG.family.asic.name);

    char * username = POOL_MODULE.is_using_fallback ?
                      POOL_MODULE.fallback_pool_user : POOL_MODULE.pool_user;
    char * password = POOL_MODULE.is_using_fallback ?
                      POOL_MODULE.fallback_pool_pass : POOL_MODULE.pool_pass;

    authorize_message_id = send_uid++;
    STRATUM_V1_authorize(*sock,
                         authorize_message_id,
                         username, password);
    STRATUM_V1_stamp_tx(authorize_message_id);
    return send_uid;
}

void decode_mining_notification(const mining_notify * mining_notification, char *extranonce_str, int extranonce_2_len)
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
            (strlen(extranonce_str) / 2) -
            extranonce_2_len;
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

bool is_wifi_connected()
{
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}



void stratum_close_connection(int * sock)
{
    if (*sock < 0) {
        ESP_LOGE(TAG, "Socket already shutdown, not shutting down again..");
        return;
    }

    ESP_LOGE(TAG, "Shutting down socket and restarting...");
    shutdown(*sock, SHUT_RDWR);
    close(*sock);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

int stratum_submit_share_(char * jobid, char * extranonce2,
                         uint32_t ntime, uint32_t nonce, uint32_t version, int * sock,int send_uid)
{
    char * user = POOL_MODULE.is_using_fallback ? POOL_MODULE.fallback_pool_user : POOL_MODULE.pool_user;
    int ret = STRATUM_V1_submit_share(*sock, send_uid, user,
                                      jobid, extranonce2, ntime, nonce, version);

    if (ret < 0) {
        ESP_LOGI(TAG, "Unable to write share to socket. Closing connection. Ret: %d (errno %d: %s)",
                 ret, errno, strerror(errno));
        stratum_close_connection(sock);
    }
    return ret;
}


#define MAX_CRITICAL_RETRY_ATTEMPTS 5

bool connect_to_stratum_server(char * stratum_url, uint16_t port, int * retry_attempts, int * retry_critical_attempts, int * sock)
{
    struct hostent * dns_addr = gethostbyname(stratum_url);
    struct timeval tcp_snd_timeout = {.tv_sec = 5, .tv_usec = 0};
    struct timeval tcp_rcv_timeout = {.tv_sec = 60 * 10, .tv_usec = 0};
    char host_ip[20];
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

    *sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    vTaskDelay(pdMS_TO_TICKS(300));
    if (*sock < 0) {
        ESP_LOGE(TAG,
                 "Unable to create socket: errno %d",
                 errno);
        if (++*retry_critical_attempts > MAX_CRITICAL_RETRY_ATTEMPTS) {
            ESP_LOGE(TAG,
                     "Max retry attempts reached, restarting...");
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
        return false;
    }
    *retry_critical_attempts = 0;

    ESP_LOGI(TAG,
             "Socket created, connecting to %s:%d",
             host_ip, port);
    int err = connect(*sock,
                      (struct sockaddr *) &dest_addr,
                      sizeof(struct sockaddr_in));
    if (err != 0) {
        (*retry_attempts)++;
        ESP_LOGE(TAG,
                 "Socket unable to connect to %s:%d "
                 "(errno %d: %s)",
                 stratum_url, port,
                 errno, strerror(errno));
        shutdown(*sock, SHUT_RDWR);
        close(*sock);
        vTaskDelay(pdMS_TO_TICKS(5000));
        return false;
    }

    if (setsockopt(*sock,
                   SOL_SOCKET,
                   SO_SNDTIMEO,
                   &tcp_snd_timeout,
                   sizeof(tcp_snd_timeout)) != 0) {
        ESP_LOGE(TAG, "Fail to setsockopt SO_SNDTIMEO");
    }

    if (setsockopt(*sock,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   &tcp_rcv_timeout,
                   sizeof(tcp_rcv_timeout)) != 0) {
        ESP_LOGE(TAG, "Fail to setsockopt SO_RCVTIMEO ");
    }
    return true;
}