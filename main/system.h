#ifndef SYSTEM_H_
#define SYSTEM_H_

#include "esp_err.h"
#include "global_state.h"

void SYSTEM_init_system();
esp_err_t SYSTEM_init_peripherals();

void SYSTEM_notify_accepted_share();
void SYSTEM_notify_rejected_share( char * error_msg);
void SYSTEM_notify_found_nonce( double found_diff, uint8_t job_id);
void SYSTEM_notify_mining_started();
void SYSTEM_notify_new_ntime( uint32_t ntime);

#endif /* SYSTEM_H_ */
