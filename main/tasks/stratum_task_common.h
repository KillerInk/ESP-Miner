#ifndef STRATUM_TASK_COMMON_H_
#define STRATUM_TASK_COMMON_H_
#include "stdint.h"
#include "stratum_api.h"

int send_initial_messages(int authorize_message_id, int * sock, uint32_t version_mask);
void decode_mining_notification(const mining_notify * mining_notification, char *extranonce_str, int extranonce_2_len);
bool is_wifi_connected();
void stratum_close_connection(int * sock);
int stratum_submit_share_(char * jobid, char * extranonce2,
                         uint32_t ntime, uint32_t nonce, uint32_t version, int * sock,int send_uid);
bool connect_to_stratum_server(char * stratum_url, uint16_t port, int * retry_attempts, int * retry_critical_attempts,int * sock);
#endif