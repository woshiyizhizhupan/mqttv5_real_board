#include "crc.h"
#include "hc32f460.h"

void crc_init()
{
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_CRC, ENABLE);
    stc_crc_init_t stcCrcInit;
    stcCrcInit.u32Protocol  = CRC_CRC32;
    stcCrcInit.u32InitValue = 0xFFFFFFFF;
    stcCrcInit.u32RefIn     = CRC_REFIN_DISABLE;
    stcCrcInit.u32RefOut    = CRC_REFOUT_DISABLE;
    stcCrcInit.u32XorOut    = CRC_XOROUT_DISABLE;
    CRC_Init(&stcCrcInit);
}
void crc_reset()
{
    CM_CRC->RESLT = 0xFFFFFFFF;
}

uint32_t crc_block_calculate(uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 4) {
        CM_CRC->DAT0 = *(uint32_t *)&data[i];
    }
    return CM_CRC->RESLT;
}
uint32_t crc_block_calculate_stm32(uint32_t *ptr, uint32_t len)

{
    uint32_t	xbit;
    uint32_t	data;
    uint32_t	CRC32 = 0xFFFFFFFF;
	uint32_t bits;
	const uint32_t dwPolynomial = 0x04c11db7;
	uint32_t	i;
	
    for(i = 0;i < len;i ++)
	{
        xbit = 1 << 31;
        data = ptr[i];
        for (bits = 0; bits < 32; bits++) 
		{
            if (CRC32 & 0x80000000) {
                CRC32 <<= 1;
                CRC32 ^= dwPolynomial;
            }
            else
                CRC32 <<= 1;
            if (data & xbit)
                CRC32 ^= dwPolynomial;
 
            xbit >>= 1;
        }
    }
    return CRC32;
}