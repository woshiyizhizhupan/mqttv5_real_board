#ifndef REAL_BOARD_NTP_H
#define REAL_BOARD_NTP_H

#include "cJSON.h"
#include "stdint.h"

void real_board_ntp_init(void);
void real_board_ntp_task(void);
void real_board_ntp_append_status(cJSON *object);
uint8_t real_board_ntp_is_synced(void);

#endif
