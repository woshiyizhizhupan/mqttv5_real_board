#ifndef SYSCLK_H
#define SYSCLK_H
#include "stdint.h"

/**
 * @brief 阻塞微秒延时
 * @param us 
 */
void delay_us(uint32_t us);
/**
 * @brief 阻塞毫秒延时
 * @param ms 
 */
void delay_ms(uint32_t ms);


void sysclk_init();
#endif