#ifndef REAL_BOARD_LAMP_H
#define REAL_BOARD_LAMP_H

#include <stdint.h>

void gpio_init(void);
void real_board_lamp_init(void);
void PWM_Init(void);
void PWM_UNIT4_Init(void);
void Adjust_Lamp(uint8_t bright);
void Adjust_UNIT4_Lamp(uint8_t bright);

#endif
