#ifndef USART_H
#define USART_H
#include "stdint.h"

/**
 * @brief 调试串口
 */
void usart1_init(void);
void usart1_write(uint8_t *data, uint16_t len);
uint16_t usart1_read_available(uint8_t *data, uint16_t max_len);
/**
 * @brief SSD1单线
 */
void usart2_init(void);
void usart2_write(uint8_t *data, uint16_t len);
void usart2_read(uint8_t *data,uint16_t len,uint16_t timeout);
uint16_t usart2_read_available(uint8_t *data, uint16_t max_len);

/**
 * @brief USB虚拟串口
 */
void usart3_init(void);
void usart3_write(uint8_t *data, uint16_t len);
uint16_t usart3_read_available(uint8_t *data, uint16_t max_len);
#endif
