#include "spi.h"
#include "hc32f460.h"

#define SPI_UNIT		CM_SPI3
#define SPI_CLK			FCG1_PERIPH_SPI3

#define SPI_SS_PORT		GPIO_PORT_A
#define SPI_SS_PIN		GPIO_PIN_01
#define SPI_SS_FUNC		GPIO_FUNC_0

#define SPI_SCK_PORT	GPIO_PORT_B
#define SPI_SCK_PIN		GPIO_PIN_13
#define SPI_SCK_FUNC	GPIO_FUNC_43

#define SPI_MISO_PORT	GPIO_PORT_B
#define SPI_MISO_PIN	GPIO_PIN_14
#define SPI_MISO_FUNC	GPIO_FUNC_41

#define SPI_MOSI_PORT	GPIO_PORT_B
#define SPI_MOSI_PIN	GPIO_PIN_15
#define SPI_MOSI_FUNC	GPIO_FUNC_40

void spi_init()
{
    stc_spi_init_t stcSpiInit;
    stc_gpio_init_t stcGpioInit;

    GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinDrv = PIN_HIGH_DRV;
    GPIO_Init(SPI_SS_PORT, SPI_SS_PIN, &stcGpioInit);
    GPIO_Init(SPI_SCK_PORT, SPI_SCK_PIN, &stcGpioInit);
    GPIO_Init(SPI_MOSI_PORT, SPI_MOSI_PIN, &stcGpioInit);
    GPIO_Init(SPI_MISO_PORT, SPI_MISO_PIN, &stcGpioInit);

    /* Configure Port */
	GPIO_SetPins(SPI_SS_PORT, SPI_SS_PIN);
    GPIO_OutputCmd(SPI_SS_PORT, SPI_SS_PIN,ENABLE);
    GPIO_SetFunc(SPI_SS_PORT, SPI_SS_PIN, SPI_SS_FUNC);
    GPIO_SetFunc(SPI_SCK_PORT, SPI_SCK_PIN, SPI_SCK_FUNC);
    GPIO_SetFunc(SPI_MOSI_PORT, SPI_MOSI_PIN, SPI_MOSI_FUNC);
    GPIO_SetFunc(SPI_MISO_PORT, SPI_MISO_PIN, SPI_MISO_FUNC);

    /* Configuration SPI */
    FCG_Fcg1PeriphClockCmd(SPI_CLK, ENABLE);
    SPI_StructInit(&stcSpiInit);
    stcSpiInit.u32WireMode = SPI_4_WIRE;
    stcSpiInit.u32TransMode = SPI_FULL_DUPLEX;
    stcSpiInit.u32MasterSlave = SPI_MASTER;
    stcSpiInit.u32Parity = SPI_PARITY_INVD;
    stcSpiInit.u32SpiMode = SPI_MD_0;
    stcSpiInit.u32BaudRatePrescaler = SPI_BR_CLK_DIV16;
    stcSpiInit.u32DataBits = SPI_DATA_SIZE_8BIT;
    stcSpiInit.u32FirstBit = SPI_FIRST_MSB;
    stcSpiInit.u32FrameLevel = SPI_1_FRAME;
    SPI_Init(SPI_UNIT, &stcSpiInit);
    SPI_Cmd(SPI_UNIT, ENABLE);
}
uint8_t spi_transmit(uint8_t *txdata, uint32_t txlen, uint8_t *rxdata, uint32_t rxlen)
{
	uint32_t temp;
    for (uint16_t i = 0; i < txlen; i++)
    {
        while ((SPI_UNIT->SR & (1 << 5)) == 0);//发送满
        SPI_UNIT->DR = txdata[i];
        while ((SPI_UNIT->SR & (1 << 7)) == 0);//接收空
        temp = SPI_UNIT->DR;
    }
    for (uint16_t i = 0; i < rxlen; i++)
    {
        while ((SPI_UNIT->SR & (1 << 5)) == 0);//发送满
        SPI_UNIT->DR = 0xFFFFFFFFUL;
        while ((SPI_UNIT->SR & (1 << 7)) == 0);//接收空
        rxdata[i] = SPI_UNIT->DR;
    }
    // while((SPI_UNIT->SR & (1<<1)));
	return 0;
}
