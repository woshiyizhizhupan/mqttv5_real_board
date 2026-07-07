#include "flash.h"
#include "hc32f460.h"

void flash_erase_sector(uint32_t addr)
{
    EFM_FWMC_Cmd(ENABLE);

    EFM_SectorErase(addr);

    EFM_FWMC_Cmd(DISABLE);
}

void flash_write(uint32_t addr, uint8_t *data, uint16_t len)
{
    EFM_FWMC_Cmd(ENABLE);

    for (uint16_t i = 0; i < len; i += 4) {
        EFM_ProgramWord(addr + i, *(uint32_t *)&data[i]);
    }

    EFM_FWMC_Cmd(DISABLE);
}

void flash_read(uint32_t addr, uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        data[i] = *(uint8_t *)(addr + i);
    }
}
