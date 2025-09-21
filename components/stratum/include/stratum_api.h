#ifndef STRATUM_API_H
#define STRATUM_API_H

#include "cJSON.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include "mining_notify.h"
#include "stratum_v1_message.h"

#define MAX_MERKLE_BRANCHES 32
#define HASH_SIZE 32
#define COINBASE_SIZE 100
#define COINBASE2_SIZE 128
#define MAX_REQUEST_IDS 1024
#define MAX_EXTRANONCE_2_LEN 32



static const int  STRATUM_ID_CONFIGURE    = 1;
static const int  STRATUM_ID_SUBSCRIBE    = 2;




typedef struct {
    int64_t timestamp_us;
    bool tracking;
} RequestTiming;


void STRATUM_V1_initialize_buffer();

char *STRATUM_V1_receive_jsonrpc_line(int sockfd);

int STRATUM_V1_subscribe(int socket, int send_uid, const char * model);

void STRATUM_V1_parse(StratumApiV1Message *message, const char *stratum_json);

void STRATUM_V1_stamp_tx(int request_id);

int STRATUM_V1_authorize(int socket, int send_uid, const char *username, const char *pass);

int STRATUM_V1_configure_version_rolling(int socket, int send_uid, uint32_t * version_mask);

int STRATUM_V1_suggest_difficulty(int socket, int send_uid, uint32_t difficulty);

int STRATUM_V1_extranonce_subscribe(int socket, int send_uid);

int STRATUM_V1_submit_share(int socket, int send_uid, const char *username, const char *jobid,
                            const char *extranonce_2, const uint32_t ntime, const uint32_t nonce,
                            const uint32_t version);

double STRATUM_V1_get_response_time_ms(int request_id);

#endif // STRATUM_API_H