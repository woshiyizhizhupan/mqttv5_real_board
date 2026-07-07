#include "wiz_platform.h"
#include "hc32f460.h"
#include "driver.h"
#include "wizchip_conf.h"

void wiz_rst_int_init(void)
{
	stc_gpio_init_t stcGpioInit;
	GPIO_StructInit(&stcGpioInit);
	stcGpioInit.u16PinDrv = PIN_HIGH_DRV;
	GPIO_Init(GPIO_PORT_A, GPIO_PIN_10, &stcGpioInit);

	GPIO_SetPins(GPIO_PORT_A, GPIO_PIN_10);
	GPIO_OutputCmd(GPIO_PORT_A, GPIO_PIN_10, ENABLE);
}

void wizchip_select(void)
{
	GPIO_ResetPins(GPIO_PORT_A, GPIO_PIN_01);
}

void wizchip_deselect(void)
{
	GPIO_SetPins(GPIO_PORT_A, GPIO_PIN_01);
}

void wizchip_write_byte(uint8_t dat)
{
	spi_transmit(&dat, 1, NULL, 0);
}

uint8_t wizchip_read_byte(void)
{
	uint8_t dat;
	spi_transmit(NULL, 0, &dat, 1);
	return dat;
}

void wizchip_write_buff(uint8_t *buf, uint16_t len)
{
	spi_transmit(buf, len, NULL, 0);
}

void wizchip_read_buff(uint8_t *buf, uint16_t len)
{
	spi_transmit(NULL, 0, buf, len);
}

void wizchip_reset(void)
{
	GPIO_SetPins(GPIO_PORT_A, GPIO_PIN_10);
	delay_ms(10);
	GPIO_ResetPins(GPIO_PORT_A, GPIO_PIN_10);
	delay_ms(10);
	GPIO_SetPins(GPIO_PORT_A, GPIO_PIN_10);
	delay_ms(10);
}

void wizchip_spi_cb_reg(void)
{
	reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
	reg_wizchip_spi_cbfunc(wizchip_read_byte, wizchip_write_byte);
	reg_wizchip_spiburst_cbfunc(wizchip_read_buff, wizchip_write_buff);
}

