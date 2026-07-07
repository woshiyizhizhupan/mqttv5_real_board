#ifndef REAL_BOARD_HARDWARE_H
#define REAL_BOARD_HARDWARE_H

#include "stdint.h"

void real_board_hardware_init(void);
void real_board_hardware_task(void);
void real_board_hlw8112_init_enable(void);
uint8_t real_board_rs485_send(uint8_t *data, uint16_t len);
uint8_t real_board_hardware_passthrough_a5(uint8_t cmd_type, const uint8_t *payload, uint16_t payload_len);

#endif
