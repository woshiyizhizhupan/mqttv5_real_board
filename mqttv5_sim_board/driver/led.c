#include "led.h"

#define LEDR_PORT GPIO_PORT_A
#define LEDR_PIN  GPIO_PIN_07

#define LEDG_PORT GPIO_PORT_B
#define LEDG_PIN  GPIO_PIN_00


void led_init()
{
    stc_gpio_init_t gpio_initsrt;
    GPIO_StructInit(&gpio_initsrt);
    gpio_initsrt.u16PinDrv        = PIN_HIGH_DRV;
    gpio_initsrt.u16PinDir        = PIN_DIR_OUT;
    GPIO_Init(LEDR_PORT, LEDR_PIN, &gpio_initsrt);
    GPIO_Init(LEDG_PORT, LEDG_PIN, &gpio_initsrt);
    GPIO_ResetPins(LEDR_PORT, LEDR_PIN);
    GPIO_ResetPins(LEDG_PORT, LEDG_PIN);
}

void led_ctrl(uint8_t led, uint8_t ctrl)
{
    switch (led) {
        case 0:
            if (ctrl)
                GPIO_SetPins(LEDR_PORT, LEDR_PIN);
            else
                GPIO_ResetPins(LEDR_PORT, LEDR_PIN);
            break;
        case 1:
            if (ctrl)
                GPIO_SetPins(LEDG_PORT, LEDG_PIN);
            else
                GPIO_ResetPins(LEDG_PORT, LEDG_PIN);
            break;
        default:
            break;
    }
}
void led_toggle(uint8_t led)
{
    switch (led) {
        case 0:
            GPIO_TogglePins(LEDR_PORT, LEDR_PIN);
            break;
        case 1:
            GPIO_TogglePins(LEDG_PORT, LEDG_PIN);
            break;
        default:
            break;
    }
}