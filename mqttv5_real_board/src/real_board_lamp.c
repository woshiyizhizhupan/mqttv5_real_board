#include "real_board_lamp.h"

#include "driver.h"
#include "hc32f460.h"
#include "hc32_ll_fcg.h"
#include "hc32_ll_tmra.h"

#define REAL_BOARD_GPIO_IN_PORT GPIO_PORT_A
#define REAL_BOARD_GPIO_IN_PIN GPIO_PIN_04
#define REAL_BOARD_LED1_PORT GPIO_PORT_A
#define REAL_BOARD_LED1_PIN GPIO_PIN_05
#define REAL_BOARD_LED2_PORT GPIO_PORT_A
#define REAL_BOARD_LED2_PIN GPIO_PIN_06
#define REAL_BOARD_LED3_PORT GPIO_PORT_A
#define REAL_BOARD_LED3_PIN GPIO_PIN_07
#define REAL_BOARD_GPIO_OUT_PORT GPIO_PORT_A
#define REAL_BOARD_GPIO_OUT_PIN GPIO_PIN_09
#define REAL_BOARD_RESET_W5500_PORT GPIO_PORT_A
#define REAL_BOARD_RESET_W5500_PIN GPIO_PIN_10
#define REAL_BOARD_INT2_1_PORT GPIO_PORT_A
#define REAL_BOARD_INT2_1_PIN GPIO_PIN_11
#define REAL_BOARD_SWLED1_PORT GPIO_PORT_A
#define REAL_BOARD_SWLED1_PIN GPIO_PIN_12
#define REAL_BOARD_SWH1_PORT GPIO_PORT_A
#define REAL_BOARD_SWH1_PIN GPIO_PIN_15
#define REAL_BOARD_SWL1_PORT GPIO_PORT_B
#define REAL_BOARD_SWL1_PIN GPIO_PIN_02
#define REAL_BOARD_SWL2_PORT GPIO_PORT_B
#define REAL_BOARD_SWL2_PIN GPIO_PIN_03
#define REAL_BOARD_SWL3_PORT GPIO_PORT_B
#define REAL_BOARD_SWL3_PIN GPIO_PIN_04
#define REAL_BOARD_SWL4_PORT GPIO_PORT_B
#define REAL_BOARD_SWL4_PIN GPIO_PIN_05
#define REAL_BOARD_RE_485_1_PORT GPIO_PORT_C
#define REAL_BOARD_RE_485_1_PIN GPIO_PIN_13
#define REAL_BOARD_SWLED2_PORT GPIO_PORT_C
#define REAL_BOARD_SWLED2_PIN GPIO_PIN_15
#define REAL_BOARD_RS485_EN_PORT GPIO_PORT_H
#define REAL_BOARD_RS485_EN_PIN GPIO_PIN_02
#define REAL_BOARD_LAMP_PWM_FUNC GPIO_FUNC_4
#define REAL_BOARD_TIMERA_COUNT_OVERFLOW (SystemCoreClock / 2U / 8U / 100U / 200U)
#define REAL_BOARD_LAMP1_PWM_UNIT CM_TMRA_2
#define REAL_BOARD_LAMP1_PWM_CLOCK FCG2_PERIPH_TMRA_2
#define REAL_BOARD_LAMP1_PWM_CHANNEL TMRA_CH1
#define REAL_BOARD_LAMP1_PWM_PORT GPIO_PORT_A
#define REAL_BOARD_LAMP1_PWM_PIN GPIO_PIN_00
#define REAL_BOARD_LAMP2_PWM_UNIT CM_TMRA_4
#define REAL_BOARD_LAMP2_PWM_CLOCK FCG2_PERIPH_TMRA_4
#define REAL_BOARD_LAMP2_PWM_CHANNEL TMRA_CH5
#define REAL_BOARD_LAMP2_PWM_PORT GPIO_PORT_C
#define REAL_BOARD_LAMP2_PWM_PIN GPIO_PIN_14

static uint8_t s_lamp_gpio_ready = 0U;
static uint8_t s_lamp_pwm_ready = 0U;
static uint32_t s_lamp1_period = 0U;
static uint32_t s_lamp2_period = 0U;

static void real_board_gpio_output_init(uint8_t port, uint16_t pin, uint16_t drv)
{
    stc_gpio_init_t gpio_init_cfg;

    GPIO_StructInit(&gpio_init_cfg);
    gpio_init_cfg.u16PinDir = PIN_DIR_OUT;
    gpio_init_cfg.u16PinDrv = drv;
    GPIO_Init(port, pin, &gpio_init_cfg);
}

static void real_board_gpio_input_init(uint8_t port, uint16_t pin)
{
    stc_gpio_init_t gpio_init_cfg;

    GPIO_StructInit(&gpio_init_cfg);
    gpio_init_cfg.u16PinDir = PIN_DIR_IN;
    GPIO_Init(port, pin, &gpio_init_cfg);
}

void gpio_init(void)
{
    real_board_gpio_output_init(REAL_BOARD_LED1_PORT, REAL_BOARD_LED1_PIN, PIN_HIGH_DRV);
    real_board_gpio_output_init(REAL_BOARD_LED2_PORT, REAL_BOARD_LED2_PIN, PIN_HIGH_DRV);
    real_board_gpio_output_init(REAL_BOARD_LED3_PORT, REAL_BOARD_LED3_PIN, PIN_HIGH_DRV);
    real_board_gpio_output_init(REAL_BOARD_RESET_W5500_PORT, REAL_BOARD_RESET_W5500_PIN, PIN_HIGH_DRV);
    real_board_gpio_output_init(REAL_BOARD_SWLED1_PORT, REAL_BOARD_SWLED1_PIN, PIN_LOW_DRV);
    real_board_gpio_output_init(REAL_BOARD_SWH1_PORT, REAL_BOARD_SWH1_PIN, PIN_LOW_DRV);
    real_board_gpio_output_init(REAL_BOARD_SWL1_PORT, REAL_BOARD_SWL1_PIN, PIN_LOW_DRV);
    real_board_gpio_output_init(REAL_BOARD_SWL2_PORT, REAL_BOARD_SWL2_PIN, PIN_LOW_DRV);
    real_board_gpio_output_init(REAL_BOARD_SWL3_PORT, REAL_BOARD_SWL3_PIN, PIN_LOW_DRV);
    real_board_gpio_output_init(REAL_BOARD_SWL4_PORT, REAL_BOARD_SWL4_PIN, PIN_LOW_DRV);
    real_board_gpio_output_init(REAL_BOARD_SWLED2_PORT, REAL_BOARD_SWLED2_PIN, PIN_LOW_DRV);
    real_board_gpio_output_init(REAL_BOARD_RS485_EN_PORT, REAL_BOARD_RS485_EN_PIN, PIN_LOW_DRV);
    real_board_gpio_output_init(REAL_BOARD_GPIO_OUT_PORT, REAL_BOARD_GPIO_OUT_PIN, PIN_LOW_DRV);
    real_board_gpio_output_init(REAL_BOARD_RE_485_1_PORT, REAL_BOARD_RE_485_1_PIN, PIN_LOW_DRV);

    real_board_gpio_input_init(REAL_BOARD_GPIO_IN_PORT, REAL_BOARD_GPIO_IN_PIN);
    real_board_gpio_input_init(REAL_BOARD_INT2_1_PORT, REAL_BOARD_INT2_1_PIN);

    GPIO_SetPins(REAL_BOARD_LED1_PORT, REAL_BOARD_LED1_PIN);
    GPIO_SetPins(REAL_BOARD_LED2_PORT, REAL_BOARD_LED2_PIN);
    GPIO_SetPins(REAL_BOARD_LED3_PORT, REAL_BOARD_LED3_PIN);
    GPIO_ResetPins(REAL_BOARD_RESET_W5500_PORT, REAL_BOARD_RESET_W5500_PIN);
    GPIO_ResetPins(REAL_BOARD_SWLED1_PORT, REAL_BOARD_SWLED1_PIN);
    GPIO_ResetPins(REAL_BOARD_SWH1_PORT, REAL_BOARD_SWH1_PIN);
    GPIO_SetPins(REAL_BOARD_SWL1_PORT, REAL_BOARD_SWL1_PIN);
    GPIO_SetPins(REAL_BOARD_SWL2_PORT, REAL_BOARD_SWL2_PIN);
    GPIO_SetPins(REAL_BOARD_SWL3_PORT, REAL_BOARD_SWL3_PIN);
    GPIO_SetPins(REAL_BOARD_SWL4_PORT, REAL_BOARD_SWL4_PIN);
    GPIO_ResetPins(REAL_BOARD_SWLED2_PORT, REAL_BOARD_SWLED2_PIN);
    GPIO_SetPins(REAL_BOARD_RS485_EN_PORT, REAL_BOARD_RS485_EN_PIN);
    GPIO_SetPins(REAL_BOARD_GPIO_OUT_PORT, REAL_BOARD_GPIO_OUT_PIN);
    GPIO_ResetPins(REAL_BOARD_RE_485_1_PORT, REAL_BOARD_RE_485_1_PIN);

    s_lamp_gpio_ready = 1U;
}

static void real_board_lamp_pwm_config(CM_TMRA_TypeDef *tmra,
                                       uint32_t channel,
                                       uint32_t *period_out)
{
    stc_tmra_init_t tmra_init;
    stc_tmra_pwm_init_t pwm_init;

    TMRA_StructInit(&tmra_init);
    tmra_init.u8CountSrc = TMRA_CNT_SRC_SW;
    tmra_init.sw_count.u8ClockDiv = TMRA_CLK_DIV8;
    tmra_init.sw_count.u8CountMode = TMRA_MD_SAWTOOTH;
    tmra_init.sw_count.u8CountDir = TMRA_DIR_UP;
    tmra_init.u32PeriodValue = REAL_BOARD_TIMERA_COUNT_OVERFLOW;
    TMRA_Init(tmra, &tmra_init);

    TMRA_PWM_StructInit(&pwm_init);
    pwm_init.u32CompareValue = (tmra_init.u32PeriodValue * 4U) / 5U;
    pwm_init.u16StartPolarity = TMRA_PWM_HIGH;
    pwm_init.u16StopPolarity = TMRA_PWM_LOW;
    pwm_init.u16CompareMatchPolarity = TMRA_PWM_LOW;
    pwm_init.u16PeriodMatchPolarity = TMRA_PWM_INVT;
    TMRA_PWM_Init(tmra, channel, &pwm_init);
    TMRA_PWM_OutputCmd(tmra, channel, ENABLE);
    TMRA_Stop(tmra);

    *period_out = TMRA_GetPeriodValue(tmra);
}

void PWM_Init(void)
{
    FCG_Fcg2PeriphClockCmd(REAL_BOARD_LAMP1_PWM_CLOCK, ENABLE);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_AOS, ENABLE);
    GPIO_SetFunc(REAL_BOARD_LAMP1_PWM_PORT, REAL_BOARD_LAMP1_PWM_PIN, REAL_BOARD_LAMP_PWM_FUNC);
    real_board_lamp_pwm_config(REAL_BOARD_LAMP1_PWM_UNIT,
                               REAL_BOARD_LAMP1_PWM_CHANNEL,
                               &s_lamp1_period);
}

void PWM_UNIT4_Init(void)
{
    FCG_Fcg2PeriphClockCmd(REAL_BOARD_LAMP2_PWM_CLOCK, ENABLE);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_AOS, ENABLE);
    GPIO_SetFunc(REAL_BOARD_LAMP2_PWM_PORT, REAL_BOARD_LAMP2_PWM_PIN, REAL_BOARD_LAMP_PWM_FUNC);
    real_board_lamp_pwm_config(REAL_BOARD_LAMP2_PWM_UNIT,
                               REAL_BOARD_LAMP2_PWM_CHANNEL,
                               &s_lamp2_period);
}

void real_board_lamp_init(void)
{
    gpio_init();
    PWM_Init();
    PWM_UNIT4_Init();
    s_lamp_pwm_ready = 1U;
}

static void real_board_lamp_ensure_gpio_ready(void)
{
    if (s_lamp_gpio_ready == 0U)
    {
        gpio_init();
    }
    if (s_lamp_pwm_ready == 0U)
    {
        PWM_Init();
        PWM_UNIT4_Init();
        s_lamp_pwm_ready = 1U;
    }
}

static void real_board_lamp_set_pwm(CM_TMRA_TypeDef *tmra,
                                    uint32_t channel,
                                    uint32_t period,
                                    uint8_t bright)
{
    uint32_t duty_cycle;

    duty_cycle = (period * (100U - (((uint32_t)bright * 85U) / 100U))) / 100U;
    if (duty_cycle == 0U)
    {
        TMRA_Stop(tmra);
    }
    else
    {
        TMRA_SetCompareValue(tmra, channel, duty_cycle - 1U);
        TMRA_PWM_OutputCmd(tmra, channel, ENABLE);
        TMRA_Start(tmra);
    }
}

void Adjust_Lamp(uint8_t bright)
{
    real_board_lamp_ensure_gpio_ready();

    if (bright != 0U)
    {
        GPIO_SetPins(REAL_BOARD_SWLED1_PORT, REAL_BOARD_SWLED1_PIN);
        delay_ms(50U);
    }
    real_board_lamp_set_pwm(REAL_BOARD_LAMP1_PWM_UNIT,
                            REAL_BOARD_LAMP1_PWM_CHANNEL,
                            s_lamp1_period,
                            bright);

    if (bright == 0U)
    {
        delay_ms(50U);
        GPIO_ResetPins(REAL_BOARD_SWLED1_PORT, REAL_BOARD_SWLED1_PIN);
    }
}

void Adjust_UNIT4_Lamp(uint8_t bright)
{
    real_board_lamp_ensure_gpio_ready();

    if (bright != 0U)
    {
        GPIO_SetPins(REAL_BOARD_SWLED2_PORT, REAL_BOARD_SWLED2_PIN);
        delay_ms(50U);
    }
    real_board_lamp_set_pwm(REAL_BOARD_LAMP2_PWM_UNIT,
                            REAL_BOARD_LAMP2_PWM_CHANNEL,
                            s_lamp2_period,
                            bright);

    if (bright == 0U)
    {
        delay_ms(50U);
        GPIO_ResetPins(REAL_BOARD_SWLED2_PORT, REAL_BOARD_SWLED2_PIN);
    }
}
