#ifndef LED_H
#define LED_H
#include "hc32f460.h"
void led_init();
void led_ctrl(uint8_t led,uint8_t ctrl);
void led_toggle(uint8_t led);
#endif