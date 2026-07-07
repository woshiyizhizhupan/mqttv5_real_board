#include "usart.h"
#include "hc32f460.h"
#include "driver.h"

#include <string.h>

#define REAL_BOARD_USART_RX_BUF_SIZE 512U

#define REAL_BOARD_USART1_BAUD 115200U
#define REAL_BOARD_USART2_BAUD 4800U
#define REAL_BOARD_USART3_BAUD 9600U

#define REAL_BOARD_USART1_TX_PORT GPIO_PORT_B
#define REAL_BOARD_USART1_TX_PIN GPIO_PIN_00
#define REAL_BOARD_USART1_RX_PORT GPIO_PORT_B
#define REAL_BOARD_USART1_RX_PIN GPIO_PIN_01

#define REAL_BOARD_USART2_TX_PORT GPIO_PORT_A
#define REAL_BOARD_USART2_TX_PIN GPIO_PIN_02
#define REAL_BOARD_USART2_RX_PORT GPIO_PORT_A
#define REAL_BOARD_USART2_RX_PIN GPIO_PIN_03

#define REAL_BOARD_USART3_RX_PORT GPIO_PORT_B
#define REAL_BOARD_USART3_RX_PIN GPIO_PIN_10
#define REAL_BOARD_USART3_TX_PORT GPIO_PORT_B
#define REAL_BOARD_USART3_TX_PIN GPIO_PIN_12

typedef struct
{
    CM_USART_TypeDef *instance;
    uint8_t rx_buf[REAL_BOARD_USART_RX_BUF_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
} real_board_usart_t;

static real_board_usart_t s_usart1 = {CM_USART1, {0}, 0U, 0U};
static real_board_usart_t s_usart2 = {CM_USART2, {0}, 0U, 0U};
static real_board_usart_t s_usart3 = {CM_USART3, {0}, 0U, 0U};

static void real_board_usart_rx_push(real_board_usart_t *handle, uint8_t data)
{
    uint16_t next_tail = (uint16_t)((handle->rx_tail + 1U) % REAL_BOARD_USART_RX_BUF_SIZE);

    if (next_tail == handle->rx_head)
    {
        handle->rx_head = (uint16_t)((handle->rx_head + 1U) % REAL_BOARD_USART_RX_BUF_SIZE);
    }

    handle->rx_buf[handle->rx_tail] = data;
    handle->rx_tail = next_tail;
}

static uint16_t real_board_usart_read_available(real_board_usart_t *handle, uint8_t *data, uint16_t max_len)
{
    uint16_t len = 0U;

    if (data == NULL)
        return 0U;

    while ((handle->rx_head != handle->rx_tail) && (len < max_len))
    {
        data[len++] = handle->rx_buf[handle->rx_head];
        handle->rx_head = (uint16_t)((handle->rx_head + 1U) % REAL_BOARD_USART_RX_BUF_SIZE);
    }

    return len;
}

static void real_board_usart_rx_callback(real_board_usart_t *handle)
{
    uint8_t data = (uint8_t)USART_ReadData(handle->instance);
    real_board_usart_rx_push(handle, data);
}

static void usart1_callback(void)
{
    real_board_usart_rx_callback(&s_usart1);
}

static void usart2_callback(void)
{
    real_board_usart_rx_callback(&s_usart2);
}

static void usart3_callback(void)
{
    real_board_usart_rx_callback(&s_usart3);
}

static void real_board_usart_irq_init(IRQn_Type irq, en_int_src_t source, func_ptr_t callback)
{
    stc_irq_signin_config_t irq_config;

    memset(&irq_config, 0, sizeof(irq_config));
    irq_config.enIRQn = irq;
    irq_config.enIntSrc = source;
    irq_config.pfnCallback = callback;
    INTC_IrqSignIn(&irq_config);
    NVIC_ClearPendingIRQ(irq_config.enIRQn);
    NVIC_SetPriority(irq_config.enIRQn, DDL_IRQ_PRIO_DEFAULT);
    NVIC_EnableIRQ(irq_config.enIRQn);
}

static void real_board_usart_init(CM_USART_TypeDef *instance,
                                  uint32_t clock,
                                  uint32_t baudrate,
                                  uint32_t parity,
                                  uint16_t tx_port,
                                  uint16_t tx_pin,
                                  uint16_t rx_port,
                                  uint16_t rx_pin,
                                  uint16_t tx_func,
                                  uint16_t rx_func,
                                  IRQn_Type irq,
                                  en_int_src_t source,
                                  func_ptr_t callback)
{
    stc_usart_uart_init_t uart_config;

    FCG_Fcg1PeriphClockCmd(clock, ENABLE);

    GPIO_SetFunc(tx_port, tx_pin, tx_func);
    GPIO_SetFunc(rx_port, rx_pin, rx_func);

    USART_UART_StructInit(&uart_config);
    uart_config.u32ClockDiv = USART_CLK_DIV16;
    uart_config.u32Baudrate = baudrate;
    uart_config.u32OverSampleBit = USART_OVER_SAMPLE_8BIT;
    uart_config.u32Parity = parity;
    USART_UART_Init(instance, &uart_config, NULL);
    USART_FuncCmd(instance, (USART_RX | USART_INT_RX | USART_TX), ENABLE);

    real_board_usart_irq_init(irq, source, callback);
}

void usart1_init(void)
{
    s_usart1.rx_head = 0U;
    s_usart1.rx_tail = 0U;
    real_board_usart_init(CM_USART1,
                          FCG1_PERIPH_USART1,
                          REAL_BOARD_USART1_BAUD,
                          USART_PARITY_NONE,
                          REAL_BOARD_USART1_TX_PORT,
                          REAL_BOARD_USART1_TX_PIN,
                          REAL_BOARD_USART1_RX_PORT,
                          REAL_BOARD_USART1_RX_PIN,
                          GPIO_FUNC_32,
                          GPIO_FUNC_33,
                          INT000_IRQn,
                          INT_SRC_USART1_RI,
                          usart1_callback);
}

void usart2_init(void)
{
    s_usart2.rx_head = 0U;
    s_usart2.rx_tail = 0U;
    real_board_usart_init(CM_USART2,
                          FCG1_PERIPH_USART2,
                          REAL_BOARD_USART2_BAUD,
                          USART_PARITY_NONE,
                          REAL_BOARD_USART2_TX_PORT,
                          REAL_BOARD_USART2_TX_PIN,
                          REAL_BOARD_USART2_RX_PORT,
                          REAL_BOARD_USART2_RX_PIN,
                          GPIO_FUNC_36,
                          GPIO_FUNC_37,
                          INT001_IRQn,
                          INT_SRC_USART2_RI,
                          usart2_callback);
}

void usart3_init(void)
{
    s_usart3.rx_head = 0U;
    s_usart3.rx_tail = 0U;
    real_board_usart_init(CM_USART3,
                          FCG1_PERIPH_USART3,
                          REAL_BOARD_USART3_BAUD,
                          USART_PARITY_EVEN,
                          REAL_BOARD_USART3_TX_PORT,
                          REAL_BOARD_USART3_TX_PIN,
                          REAL_BOARD_USART3_RX_PORT,
                          REAL_BOARD_USART3_RX_PIN,
                          GPIO_FUNC_32,
                          GPIO_FUNC_33,
                          INT002_IRQn,
                          INT_SRC_USART3_RI,
                          usart3_callback);
}

static void real_board_usart_write(CM_USART_TypeDef *instance, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0U; i < len; i++)
    {
        USART_WriteData(instance, data[i]);
        while (USART_GetStatus(instance, USART_FLAG_TX_CPLT) == RESET)
            ;
    }
}

void usart1_write(uint8_t *data, uint16_t len)
{
    real_board_usart_write(CM_USART1, data, len);
}

void usart2_write(uint8_t *data, uint16_t len)
{
    real_board_usart_write(CM_USART2, data, len);
}

void usart3_write(uint8_t *data, uint16_t len)
{
    real_board_usart_write(CM_USART3, data, len);
}

uint16_t usart1_read_available(uint8_t *data, uint16_t max_len)
{
    return real_board_usart_read_available(&s_usart1, data, max_len);
}

uint16_t usart2_read_available(uint8_t *data, uint16_t max_len)
{
    return real_board_usart_read_available(&s_usart2, data, max_len);
}

uint16_t usart3_read_available(uint8_t *data, uint16_t max_len)
{
    return real_board_usart_read_available(&s_usart3, data, max_len);
}

void usart2_read(uint8_t *data, uint16_t len, uint16_t timeout)
{
    uint16_t read_len = 0U;

    while ((read_len < len) && (timeout > 0U))
    {
        read_len += usart2_read_available(&data[read_len], (uint16_t)(len - read_len));
        if (read_len >= len)
            break;
        timeout--;
        delay_ms(1);
    }
}

void print_char(uint8_t ch)
{
    USART_WriteData(CM_USART1, ch);
    while (USART_GetStatus(CM_USART1, USART_FLAG_TX_CPLT) == RESET)
        ;
}
