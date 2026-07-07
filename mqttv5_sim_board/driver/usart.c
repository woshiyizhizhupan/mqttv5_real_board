#include "usart.h"
#include "hc32f460.h"
#include "driver.h"

typedef struct
{
    uint16_t rx_len;
    uint8_t rx_buf[256];
}usart_handle_t;
usart_handle_t usart1_handle;
static void usart1_callback(void)
{
    if(usart1_handle.rx_len<256)
    {
        usart1_handle.rx_buf[usart1_handle.rx_len] = USART_ReadData(CM_USART1);
        usart1_handle.rx_len++;
    }
}


void usart1_init()
{
    stc_irq_signin_config_t stcIrqSigninConfig;
    stcIrqSigninConfig.enIRQn      = INT000_IRQn;
    stcIrqSigninConfig.enIntSrc    = INT_SRC_USART1_RI;
    stcIrqSigninConfig.pfnCallback = &usart1_callback;
    INTC_IrqSignIn(&stcIrqSigninConfig);
    NVIC_ClearPendingIRQ(stcIrqSigninConfig.enIRQn);
    NVIC_SetPriority(stcIrqSigninConfig.enIRQn, DDL_IRQ_PRIO_DEFAULT);
    NVIC_EnableIRQ(stcIrqSigninConfig.enIRQn);

    FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_USART1, ENABLE);
    stc_usart_uart_init_t stcUartInit;
    USART_UART_StructInit(&stcUartInit);
    stcUartInit.u32ClockDiv      = USART_CLK_DIV16;
    stcUartInit.u32Baudrate      = 115200;
    stcUartInit.u32OverSampleBit = USART_OVER_SAMPLE_8BIT;
    USART_UART_Init(CM_USART1, &stcUartInit, NULL);
    USART_FuncCmd(CM_USART1, (USART_RX | USART_INT_RX | USART_TX), ENABLE);
    GPIO_SetFunc(GPIO_PORT_A,GPIO_PIN_04,GPIO_FUNC_32);
    GPIO_SetFunc(GPIO_PORT_A,GPIO_PIN_05,GPIO_FUNC_33);
}

void usart1_write(uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        USART_WriteData(CM_USART1, data[i]);
        while (USART_GetStatus(CM_USART1, USART_FLAG_TX_CPLT) == RESET);
    }
}

void print_char(uint8_t ch)
{
    USART_WriteData(CM_USART1, ch);
    while (USART_GetStatus(CM_USART1, USART_FLAG_TX_CPLT) == RESET);
}

