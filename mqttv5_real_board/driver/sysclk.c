#include "sysclk.h"
#include "hc32f460.h"

void delay_us(uint32_t us)
{
    uint32_t temp;
    if (us == 0) return;
    SysTick->LOAD = us * 200;
    SysTick->VAL  = 0;
    SysTick->CTRL = (1 << 0) + (1 << 2);
    do {
        temp = SysTick->CTRL;
    } while ((temp & (1 << 16)) == 0);
    SysTick->CTRL = 0;
}
void delay_ms(uint32_t ms)
{
    while (ms > 10) {
        delay_us(10000);
        ms -= 10;
    }
    delay_us(ms * 1000);
}

/**
 * @brief 系统时钟初始化
 *
 */
void sysclk_init()
{
    /* Set bus clock div. */
    CLK_SetClockDiv(CLK_BUS_CLK_ALL, (CLK_HCLK_DIV1 | CLK_EXCLK_DIV2 | CLK_PCLK0_DIV1 | CLK_PCLK1_DIV2 |
                                      CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 | CLK_PCLK4_DIV2));
    /* sram init include read/write wait cycle setting */
    SRAM_SetWaitCycle(SRAM_SRAM_ALL, SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);
    SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
    /* flash read wait cycle setting */
    EFM_SetWaitCycle(EFM_WAIT_CYCLE5);
    /* XTAL config */
    stc_clock_xtal_init_t stcXtalInit;
    CLK_XtalStructInit(&stcXtalInit);
    stcXtalInit.u8State      = CLK_XTAL_ON;
    stcXtalInit.u8Drv        = CLK_XTAL_DRV_HIGH;
    stcXtalInit.u8Mode       = CLK_XTAL_MD_OSC;
    stcXtalInit.u8StableTime = CLK_XTAL_STB_2MS;
    CLK_XtalInit(&stcXtalInit);
    /* MPLL config */
    stc_clock_pll_init_t stcMPLLInit;
    CLK_PLLStructInit(&stcMPLLInit);
    stcMPLLInit.PLLCFGR          = 0UL;
    stcMPLLInit.PLLCFGR_f.PLLM   = (1UL - 1UL);
    stcMPLLInit.PLLCFGR_f.PLLN   = (50UL - 1UL);
    stcMPLLInit.PLLCFGR_f.PLLP   = (2UL - 1UL);
    stcMPLLInit.PLLCFGR_f.PLLQ   = (2UL - 1UL);
    stcMPLLInit.PLLCFGR_f.PLLR   = (2UL - 1UL);
    stcMPLLInit.u8PLLState       = CLK_PLL_ON;
    stcMPLLInit.PLLCFGR_f.PLLSRC = CLK_PLL_SRC_XTAL;
    CLK_PLLInit(&stcMPLLInit);
    /* 3 cycles for 126MHz ~ 200MHz */
    GPIO_SetReadWaitCycle(GPIO_RD_WAIT3);
    /* Switch driver ability */
    PWC_HighSpeedToHighPerformance();
    /* Set the system clock source */
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);
}