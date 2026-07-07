#ifndef SPI_H
#define SPI_H
#include "stdint.h"

void spi_init();

uint8_t spi_transmit(uint8_t *txdata, uint32_t txlen, uint8_t *rxdata, uint32_t rxlen);
#endif