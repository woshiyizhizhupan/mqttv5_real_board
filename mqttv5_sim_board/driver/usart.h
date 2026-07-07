#ifndef USART_H
#define USART_H
#include "stdint.h"

/**
 * @brief 调试串口
 */
void usart1_init();
void usart1_write(uint8_t *data, uint16_t len);
/**
 * @brief SSD1单线
 */
void usart2_init();
void usart2_write(uint8_t *data, uint16_t len);
void usart2_read(uint8_t *data,uint16_t len,uint16_t timeout);

/**
 * @brief USB虚拟串口
 */
void usart3_init();
void usart3_write(uint8_t *data, uint16_t len);
#endif
