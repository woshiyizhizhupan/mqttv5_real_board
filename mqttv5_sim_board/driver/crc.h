#ifndef CRC_H
#define CRC_H
#include "stdint.h"


void crc_init();
void crc_reset();
/**
 * @brief crc32_mpeg_2
 * @param data 
 * @param len 
 * @return crc32_mpeg_2 result
 */
uint32_t crc_block_calculate(uint8_t *data,uint32_t len);

__attribute((section(".ramfunc"))) uint32_t crc_block_calculate_stm32(uint32_t *ptr, uint32_t len);
#endif