#ifndef FLASH_H
#define FLASH_H
#include "stdint.h"
void flash_erase_sector(uint32_t addr);
void flash_write(uint32_t addr ,uint8_t *data, uint16_t len);
void flash_read(uint32_t addr ,uint8_t *data, uint16_t len);
#endif