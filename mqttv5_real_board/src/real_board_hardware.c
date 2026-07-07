#include "real_board_hardware.h"

#include "real_board_business.h"
#include "driver.h"
#include "hc32f460.h"

#include <string.h>

#define REAL_BOARD_RE_485_1_PORT GPIO_PORT_C
#define REAL_BOARD_RE_485_1_PIN GPIO_PIN_13
#define REAL_BOARD_RS485_EN_PORT GPIO_PORT_H
#define REAL_BOARD_RS485_EN_PIN GPIO_PIN_02

#define REAL_BOARD_HLW8112_REG_UFREQ 0x23U
#define REAL_BOARD_HLW8112_REG_RMS_IA 0x24U
#define REAL_BOARD_HLW8112_REG_RMS_IB 0x25U
#define REAL_BOARD_HLW8112_REG_RMS_U 0x26U
#define REAL_BOARD_HLW8112_REG_POWER_FACTOR 0x27U
#define REAL_BOARD_HLW8112_REG_ENERGY_PA 0x28U
#define REAL_BOARD_HLW8112_REG_ENERGY_PB 0x29U
#define REAL_BOARD_HLW8112_REG_POWER_PA 0x2CU
#define REAL_BOARD_HLW8112_REG_POWER_PB 0x2DU
#define REAL_BOARD_HLW8112_REG_POWER_S 0x2EU
#define REAL_BOARD_HLW8112_REG_RMS_IAC 0x70U
#define REAL_BOARD_HLW8112_REG_RMS_IBC 0x71U
#define REAL_BOARD_HLW8112_REG_RMS_UC 0x72U
#define REAL_BOARD_HLW8112_REG_POWER_PAC 0x73U
#define REAL_BOARD_HLW8112_REG_POWER_PBC 0x74U
#define REAL_BOARD_HLW8112_REG_POWER_SC 0x75U
#define REAL_BOARD_HLW8112_REG_ENERGY_PAC 0x76U
#define REAL_BOARD_HLW8112_REG_ENERGY_PBC 0x77U
#define REAL_BOARD_HLW8112_REG_HFCONST 0x02U

#define REAL_BOARD_HLW8112_REG_EMUCON 0x01U
#define REAL_BOARD_HLW8112_REG_EMUCON2 0x13U
#define REAL_BOARD_HLW8112_REG_IA 0x5AU
#define REAL_BOARD_HLW8112_REG_RST 0x96U
#define REAL_BOARD_HLW8112_REG_ENABLE 0xE5U
#define REAL_BOARD_HLW8112_REG_DISABLE 0xDCU

#define REAL_BOARD_HLW8112_DEFAULT_IAK 100U
#define REAL_BOARD_HLW8112_DEFAULT_UK 50U
#define REAL_BOARD_HLW8112_READ_PERIOD_TICKS 10U
#define REAL_BOARD_HLW8112_RX_BUFFER_MAX 8U
#define REAL_BOARD_HLW8112_RESPONSE_TIMEOUT_MS 5000U
#define REAL_BOARD_RS485_PERIOD_TICKS 5000U
#define REAL_BOARD_RS485_RESPONSE_MS 100U
#define REAL_BOARD_RS485_QUEUE_DEPTH 4U
#define REAL_BOARD_RS485_QUEUE_PAYLOAD_MAX 128U
#define REAL_BOARD_RS485_RESPONSE_TIMEOUT_MS 1000U

typedef enum
{
    REAL_BOARD_HLW_READ_UFREQ = 0,
    REAL_BOARD_HLW_READ_RMS_IA,
    REAL_BOARD_HLW_READ_RMS_IB,
    REAL_BOARD_HLW_READ_RMS_U,
    REAL_BOARD_HLW_READ_POWER_FACTOR,
    REAL_BOARD_HLW_READ_ENERGY_PA,
    REAL_BOARD_HLW_READ_ENERGY_PB,
    REAL_BOARD_HLW_READ_POWER_PA,
    REAL_BOARD_HLW_READ_POWER_PB,
    REAL_BOARD_HLW_READ_POWER_S,
    REAL_BOARD_HLW_READ_RMS_IAC,
    REAL_BOARD_HLW_READ_RMS_IBC,
    REAL_BOARD_HLW_READ_RMS_UC,
    REAL_BOARD_HLW_READ_POWER_PAC,
    REAL_BOARD_HLW_READ_POWER_PBC,
    REAL_BOARD_HLW_READ_POWER_SC,
    REAL_BOARD_HLW_READ_ENERGY_PAC,
    REAL_BOARD_HLW_READ_ENERGY_PBC,
    REAL_BOARD_HLW_READ_HFCONST,
    REAL_BOARD_HLW_READ_COUNT
} real_board_hlw8112_read_order_t;

typedef enum
{
    REAL_BOARD_HLW_INIT_IDLE = 0,
    REAL_BOARD_HLW_INIT_START,
    REAL_BOARD_HLW_INIT_READING_CAL,
    REAL_BOARD_HLW_INIT_FINISH
} real_board_hlw8112_init_state_t;

typedef struct
{
    real_board_hlw8112_init_state_t init;
    uint8_t init_step;
    uint8_t read_open;
    uint8_t reg_order;
    uint8_t awaiting_order;
    uint8_t rx_len;
    uint8_t rx_buf[REAL_BOARD_HLW8112_RX_BUFFER_MAX];
    uint32_t read_tick;
    uint32_t pending_tick;

    uint16_t ufreq;
    uint32_t rms_ia;
    uint32_t rms_ib;
    uint32_t rms_u;
    uint32_t power_factor_raw;
    uint32_t energy_pa;
    uint32_t energy_pb;
    uint32_t power_pa;
    uint32_t power_pb;
    uint32_t power_s;
    uint16_t rms_iac;
    uint16_t rms_ibc;
    uint16_t rms_uc;
    uint16_t power_pac;
    uint16_t power_pbc;
    uint16_t power_sc;
    uint16_t energy_pac;
    uint16_t energy_pbc;
    uint16_t hfconst;
} real_board_hlw8112_state_t;

typedef struct
{
    uint8_t used;
    uint8_t cmd_type;
    uint16_t payload_len;
    uint8_t payload[REAL_BOARD_RS485_QUEUE_PAYLOAD_MAX];
} real_board_rs485_queue_item_t;

static real_board_hlw8112_state_t s_hlw8112;
static real_board_meter_hlw8112_t s_meter;
static real_board_environment_t s_environment;
static real_board_rs485_t s_rs485;
static uint32_t s_rs485_tick;
static real_board_rs485_queue_item_t s_rs485_queue[REAL_BOARD_RS485_QUEUE_DEPTH];
static uint8_t s_rs485_active;
static uint8_t s_rs485_active_cmd_type;
static uint32_t s_rs485_active_started_ms;
extern volatile uint32_t g_real_board_millis;

static const uint8_t s_hlw8112_read_registers[REAL_BOARD_HLW_READ_COUNT] = {
    REAL_BOARD_HLW8112_REG_UFREQ,
    REAL_BOARD_HLW8112_REG_RMS_IA,
    REAL_BOARD_HLW8112_REG_RMS_IB,
    REAL_BOARD_HLW8112_REG_RMS_U,
    REAL_BOARD_HLW8112_REG_POWER_FACTOR,
    REAL_BOARD_HLW8112_REG_ENERGY_PA,
    REAL_BOARD_HLW8112_REG_ENERGY_PB,
    REAL_BOARD_HLW8112_REG_POWER_PA,
    REAL_BOARD_HLW8112_REG_POWER_PB,
    REAL_BOARD_HLW8112_REG_POWER_S,
    REAL_BOARD_HLW8112_REG_RMS_IAC,
    REAL_BOARD_HLW8112_REG_RMS_IBC,
    REAL_BOARD_HLW8112_REG_RMS_UC,
    REAL_BOARD_HLW8112_REG_POWER_PAC,
    REAL_BOARD_HLW8112_REG_POWER_PBC,
    REAL_BOARD_HLW8112_REG_POWER_SC,
    REAL_BOARD_HLW8112_REG_ENERGY_PAC,
    REAL_BOARD_HLW8112_REG_ENERGY_PBC,
    REAL_BOARD_HLW8112_REG_HFCONST,
};

static void real_board_hardware_publish_meter(void)
{
    real_board_meter_hlw8112_t meter = s_meter;
    real_board_business_update_meter_hlw8112(&meter);
}

static void real_board_hardware_publish_environment(void)
{
    real_board_environment_t environment = s_environment;
    real_board_business_update_environment(&environment);
}

static void real_board_hardware_publish_rs485(void)
{
    real_board_rs485_t rs485 = s_rs485;
    real_board_business_update_rs485(&rs485);
}

static uint8_t real_board_hlw8112_crc_sum(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0U;

    while (len-- > 0U)
        crc = (uint8_t)(crc + *data++);

    return (uint8_t)(~crc);
}

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be24(const uint8_t *data)
{
    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

static uint32_t read_be32_abs(const uint8_t *data)
{
    uint32_t value = ((uint32_t)data[0] << 24) |
                     ((uint32_t)data[1] << 16) |
                     ((uint32_t)data[2] << 8) |
                     data[3];

    if ((value & 0x80000000UL) != 0UL)
        value = 0xFFFFFFFFUL - value;
    return value;
}

static void real_board_hlw8112_send_frame(uint8_t *frame, uint16_t len)
{
    usart3_write(frame, len);
}

static void real_board_hlw8112_send_read(uint8_t reg)
{
    uint8_t frame[2];
    uint16_t len = 0U;

    frame[len++] = 0xA5U;
    frame[len++] = reg;
    real_board_hlw8112_send_frame(frame, len);
}

static void real_board_hlw8112_send_write(uint8_t reg)
{
    uint8_t frame[4];
    uint16_t len = 0U;

    frame[len++] = 0xA5U;
    frame[len++] = 0xEAU;
    frame[len++] = reg;
    frame[len] = real_board_hlw8112_crc_sum(frame, len);
    len++;
    real_board_hlw8112_send_frame(frame, len);
}

static void real_board_hlw8112_send_write2(uint8_t reg, uint8_t data_h, uint8_t data_l)
{
    uint8_t frame[5];
    uint16_t len = 0U;

    frame[len++] = 0xA5U;
    frame[len++] = (uint8_t)(reg | 0x80U);
    frame[len++] = data_h;
    frame[len++] = data_l;
    frame[len] = real_board_hlw8112_crc_sum(frame, len);
    len++;
    real_board_hlw8112_send_frame(frame, len);
}

void real_board_hlw8112_init_enable(void)
{
    memset(&s_hlw8112, 0, sizeof(s_hlw8112));
    s_hlw8112.init = REAL_BOARD_HLW_INIT_START;

    real_board_hlw8112_send_write(REAL_BOARD_HLW8112_REG_RST);
    delay_ms(100U);
    real_board_hlw8112_send_write(REAL_BOARD_HLW8112_REG_ENABLE);
    delay_ms(50U);
    real_board_hlw8112_send_write(REAL_BOARD_HLW8112_REG_IA);
    delay_ms(50U);
    real_board_hlw8112_send_write2(REAL_BOARD_HLW8112_REG_EMUCON, 0x0CU, 0x01U);
    delay_ms(50U);
    real_board_hlw8112_send_write2(REAL_BOARD_HLW8112_REG_EMUCON2, 0x04U, 0x65U);
    delay_ms(50U);
    real_board_hlw8112_send_write(REAL_BOARD_HLW8112_REG_DISABLE);
    delay_ms(50U);

    s_hlw8112.init = REAL_BOARD_HLW_INIT_READING_CAL;
    s_hlw8112.read_open = 1U;
    s_hlw8112.read_tick = g_real_board_millis;
}

static void real_board_hlw8112_compute(void)
{
    uint32_t iak = REAL_BOARD_HLW8112_DEFAULT_IAK;
    uint32_t uk = REAL_BOARD_HLW8112_DEFAULT_UK;
    uint32_t power_s = 0U;
    uint32_t power_pa = 0U;
    uint32_t voltage_x10 = 0U;
    uint32_t current_raw = 0U;
    uint32_t energy_pa = 0U;
    uint16_t rms_iac = s_hlw8112.rms_iac ? s_hlw8112.rms_iac : 1U;
    uint16_t rms_uc = s_hlw8112.rms_uc ? s_hlw8112.rms_uc : 1U;
    uint16_t power_pac = s_hlw8112.power_pac ? s_hlw8112.power_pac : 1U;
    uint16_t power_sc = s_hlw8112.power_sc ? s_hlw8112.power_sc : 1U;
    uint16_t energy_pac = s_hlw8112.energy_pac ? s_hlw8112.energy_pac : 1U;
    uint16_t hfconst = s_hlw8112.hfconst ? s_hlw8112.hfconst : 1U;

    current_raw = (uint32_t)((uint64_t)s_hlw8112.rms_ia * rms_iac * 100ULL / iak / 0x800000ULL);
    voltage_x10 = (uint32_t)((uint64_t)s_hlw8112.rms_u * rms_uc * 10ULL / uk / 0x400000ULL);
    power_pa = (uint32_t)((uint64_t)s_hlw8112.power_pa * power_pac * 1000000ULL / iak / uk / 0x80000000ULL);
    power_s = (uint32_t)((uint64_t)s_hlw8112.power_s * power_sc * 1000000ULL / iak / uk / 0x80000000ULL);
    energy_pa = (uint32_t)((uint64_t)s_hlw8112.energy_pa * energy_pac * hfconst / iak / uk / (0x20000000000ULL / 1000000ULL));

    memset(&s_meter, 0, sizeof(s_meter));
    s_meter.valid = 1U;
    s_meter.current_ma = current_raw;
    s_meter.voltage_mv = voltage_x10 * 100U;
    s_meter.active_power_mw = power_pa * 10U;
    s_meter.reactive_power_mvar = 0U;
    s_meter.power_factor_x1000 = power_s ? (uint32_t)((uint64_t)power_pa * 1000ULL / power_s) : 0U;
    s_meter.energy_one_wh = energy_pa;
    s_meter.energy_total_wh = energy_pa;
    s_meter.pulse_count++;
    real_board_hardware_publish_meter();
}

static void real_board_hlw8112_store_response(uint8_t order, const uint8_t *data, uint16_t len)
{
    if (len < 3U)
        return;

    switch (order)
    {
    case REAL_BOARD_HLW_READ_HFCONST:
        s_hlw8112.hfconst = read_be16(&data[len - 3U]);
        break;
    case REAL_BOARD_HLW_READ_UFREQ:
        s_hlw8112.ufreq = read_be16(&data[len - 3U]);
        break;
    case REAL_BOARD_HLW_READ_RMS_IA:
        if (len >= 4U)
            s_hlw8112.rms_ia = read_be24(&data[len - 4U]);
        break;
    case REAL_BOARD_HLW_READ_RMS_IB:
        if (len >= 4U)
            s_hlw8112.rms_ib = read_be24(&data[len - 4U]);
        break;
    case REAL_BOARD_HLW_READ_RMS_U:
        if (len >= 4U)
            s_hlw8112.rms_u = read_be24(&data[len - 4U]);
        break;
    case REAL_BOARD_HLW_READ_POWER_FACTOR:
        if (len >= 4U)
            s_hlw8112.power_factor_raw = read_be24(&data[len - 4U]);
        break;
    case REAL_BOARD_HLW_READ_ENERGY_PA:
        if (len >= 4U)
            s_hlw8112.energy_pa = read_be24(&data[len - 4U]);
        break;
    case REAL_BOARD_HLW_READ_ENERGY_PB:
        if (len >= 4U)
            s_hlw8112.energy_pb = read_be24(&data[len - 4U]);
        break;
    case REAL_BOARD_HLW_READ_POWER_PA:
        if (len >= 5U)
            s_hlw8112.power_pa = read_be32_abs(&data[len - 5U]);
        break;
    case REAL_BOARD_HLW_READ_POWER_PB:
        if (len >= 5U)
            s_hlw8112.power_pb = read_be32_abs(&data[len - 5U]);
        break;
    case REAL_BOARD_HLW_READ_POWER_S:
        if (len >= 5U)
            s_hlw8112.power_s = read_be32_abs(&data[len - 5U]);
        break;
    case REAL_BOARD_HLW_READ_RMS_IAC:
        s_hlw8112.rms_iac = read_be16(&data[len - 3U]);
        break;
    case REAL_BOARD_HLW_READ_RMS_IBC:
        s_hlw8112.rms_ibc = read_be16(&data[len - 3U]);
        break;
    case REAL_BOARD_HLW_READ_RMS_UC:
        s_hlw8112.rms_uc = read_be16(&data[len - 3U]);
        break;
    case REAL_BOARD_HLW_READ_POWER_PAC:
        s_hlw8112.power_pac = read_be16(&data[len - 3U]);
        break;
    case REAL_BOARD_HLW_READ_POWER_PBC:
        s_hlw8112.power_pbc = read_be16(&data[len - 3U]);
        break;
    case REAL_BOARD_HLW_READ_POWER_SC:
        s_hlw8112.power_sc = read_be16(&data[len - 3U]);
        break;
    case REAL_BOARD_HLW_READ_ENERGY_PAC:
        s_hlw8112.energy_pac = read_be16(&data[len - 3U]);
        break;
    case REAL_BOARD_HLW_READ_ENERGY_PBC:
        s_hlw8112.energy_pbc = read_be16(&data[len - 3U]);
        break;
    default:
        break;
    }
}

static void real_board_hlw8112_handle_rx(void)
{
    uint8_t rx_buf[32];
    uint16_t len = usart3_read_available(rx_buf, sizeof(rx_buf));
    uint16_t copy_len;

    if (len == 0U)
        return;

    if (len > (uint16_t)(REAL_BOARD_HLW8112_RX_BUFFER_MAX - s_hlw8112.rx_len))
        s_hlw8112.rx_len = 0U;

    copy_len = len;
    if (copy_len > (uint16_t)(REAL_BOARD_HLW8112_RX_BUFFER_MAX - s_hlw8112.rx_len))
        copy_len = (uint16_t)(REAL_BOARD_HLW8112_RX_BUFFER_MAX - s_hlw8112.rx_len);
    if (copy_len == 0U)
        return;

    memcpy(&s_hlw8112.rx_buf[s_hlw8112.rx_len], rx_buf, copy_len);
    s_hlw8112.rx_len = (uint8_t)(s_hlw8112.rx_len + copy_len);

    if (s_hlw8112.rx_len < 2U)
        return;

    if (s_hlw8112.rx_buf[s_hlw8112.rx_len - 1U] !=
        real_board_hlw8112_crc_sum(s_hlw8112.rx_buf, (uint16_t)(s_hlw8112.rx_len - 1U)))
    {
        if (s_hlw8112.rx_len >= REAL_BOARD_HLW8112_RX_BUFFER_MAX)
        {
            s_hlw8112.rx_len = 0U;
            s_hlw8112.read_open = 1U;
        }
        return;
    }

    real_board_hlw8112_store_response(s_hlw8112.awaiting_order, s_hlw8112.rx_buf, s_hlw8112.rx_len);
    s_hlw8112.rx_len = 0U;
    s_hlw8112.read_open = 1U;
    if (s_hlw8112.reg_order < REAL_BOARD_HLW_READ_COUNT)
        s_hlw8112.reg_order++;
}

static void real_board_hlw8112_handle_timeout(void)
{
    if (s_hlw8112.read_open != 0U)
        return;
    if ((s_hlw8112.init != REAL_BOARD_HLW_INIT_READING_CAL) &&
        (s_hlw8112.init != REAL_BOARD_HLW_INIT_FINISH))
    {
        return;
    }
    if ((uint32_t)(g_real_board_millis - s_hlw8112.pending_tick) < REAL_BOARD_HLW8112_RESPONSE_TIMEOUT_MS)
        return;

    s_hlw8112.rx_len = 0U;
    s_hlw8112.reg_order = 0U;
    s_hlw8112.read_open = 1U;
}

static void real_board_hlw8112_init_task(void)
{
    if (s_hlw8112.init != REAL_BOARD_HLW_INIT_START)
        return;

    if ((uint32_t)(g_real_board_millis - s_hlw8112.read_tick) < REAL_BOARD_HLW8112_READ_PERIOD_TICKS)
        return;
    s_hlw8112.read_tick = g_real_board_millis;

    switch (s_hlw8112.init_step++)
    {
    case 0:
        real_board_hlw8112_send_write(REAL_BOARD_HLW8112_REG_RST);
        break;
    case 1:
        real_board_hlw8112_send_write(REAL_BOARD_HLW8112_REG_ENABLE);
        break;
    case 2:
        real_board_hlw8112_send_write(REAL_BOARD_HLW8112_REG_IA);
        break;
    case 3:
        real_board_hlw8112_send_write2(REAL_BOARD_HLW8112_REG_EMUCON, 0x0CU, 0x01U);
        break;
    case 4:
        real_board_hlw8112_send_write2(REAL_BOARD_HLW8112_REG_EMUCON2, 0x04U, 0x65U);
        break;
    case 5:
        real_board_hlw8112_send_write(REAL_BOARD_HLW8112_REG_DISABLE);
        break;
    default:
        s_hlw8112.init = REAL_BOARD_HLW_INIT_READING_CAL;
        s_hlw8112.reg_order = 0U;
        s_hlw8112.read_open = 1U;
        break;
    }
}

static void real_board_hlw8112_read_task(void)
{
    if ((s_hlw8112.init != REAL_BOARD_HLW_INIT_READING_CAL) &&
        (s_hlw8112.init != REAL_BOARD_HLW_INIT_FINISH))
    {
        return;
    }

    if ((uint32_t)(g_real_board_millis - s_hlw8112.read_tick) < REAL_BOARD_HLW8112_READ_PERIOD_TICKS)
        return;
    s_hlw8112.read_tick = g_real_board_millis;

    if (s_hlw8112.init == REAL_BOARD_HLW_INIT_READING_CAL)
    {
        if (s_hlw8112.reg_order >= REAL_BOARD_HLW_READ_COUNT)
        {
            s_hlw8112.init = REAL_BOARD_HLW_INIT_FINISH;
            s_hlw8112.reg_order = 0U;
            s_hlw8112.read_open = 1U;
            return;
        }
    }
    else if (s_hlw8112.reg_order > REAL_BOARD_HLW_READ_POWER_S)
    {
        s_hlw8112.reg_order = 0U;
        real_board_hlw8112_compute();
    }

    if (s_hlw8112.read_open == 0U)
        return;

    s_hlw8112.awaiting_order = s_hlw8112.reg_order;
    s_hlw8112.pending_tick = g_real_board_millis;
    s_hlw8112.rx_len = 0U;
    real_board_hlw8112_send_read(s_hlw8112_read_registers[s_hlw8112.reg_order]);
    s_hlw8112.read_open = 0U;
}

static uint16_t real_board_modbus_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;

    for (uint16_t i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x0001U) != 0U)
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            else
                crc >>= 1;
        }
    }
    return crc;
}

static uint8_t real_board_modbus_crc_ok(const uint8_t *data, uint16_t len)
{
    uint16_t expected;
    uint16_t actual;

    if (len < 3U)
        return 0U;

    actual = (uint16_t)data[len - 2U] | ((uint16_t)data[len - 1U] << 8);
    expected = real_board_modbus_crc16(data, (uint16_t)(len - 2U));
    return actual == expected;
}

static uint8_t real_board_rs485_send_now(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
        return 0U;

    GPIO_SetPins(REAL_BOARD_RS485_EN_PORT, REAL_BOARD_RS485_EN_PIN);
    GPIO_SetPins(REAL_BOARD_RE_485_1_PORT, REAL_BOARD_RE_485_1_PIN);
    delay_ms(1U);
    usart2_write((uint8_t *)data, len);
    delay_ms(2U);
    GPIO_ResetPins(REAL_BOARD_RE_485_1_PORT, REAL_BOARD_RE_485_1_PIN);

    s_rs485.valid = 1U;
    s_rs485.tx_count++;
    real_board_hardware_publish_rs485();
    return 1U;
}

uint8_t real_board_rs485_send(uint8_t *data, uint16_t len)
{
    return real_board_rs485_send_now(data, len);
}

static uint8_t real_board_rs485_enqueue(uint8_t cmd_type, const uint8_t *payload, uint16_t payload_len)
{
    if ((payload == NULL) || (payload_len == 0U) || (payload_len > REAL_BOARD_RS485_QUEUE_PAYLOAD_MAX))
    {
        s_rs485.valid = 1U;
        s_rs485.error_count++;
        real_board_hardware_publish_rs485();
        return 0U;
    }

    for (uint8_t i = 0U; i < REAL_BOARD_RS485_QUEUE_DEPTH; i++)
    {
        if (s_rs485_queue[i].used == 0U)
        {
            memset(&s_rs485_queue[i], 0, sizeof(s_rs485_queue[i]));
            s_rs485_queue[i].used = 1U;
            s_rs485_queue[i].cmd_type = cmd_type;
            s_rs485_queue[i].payload_len = payload_len;
            memcpy(s_rs485_queue[i].payload, payload, payload_len);
            return 1U;
        }
    }

    s_rs485.valid = 1U;
    s_rs485.error_count++;
    real_board_hardware_publish_rs485();
    return 0U;
}

static void real_board_rs485_start_next(void)
{
    if (s_rs485_active != 0U)
        return;

    for (uint8_t i = 0U; i < REAL_BOARD_RS485_QUEUE_DEPTH; i++)
    {
        if (s_rs485_queue[i].used == 0U)
            continue;

        s_rs485_active = 1U;
        s_rs485_active_cmd_type = s_rs485_queue[i].cmd_type;
        s_rs485_active_started_ms = g_real_board_millis;

        if (!real_board_rs485_send_now(s_rs485_queue[i].payload, s_rs485_queue[i].payload_len))
        {
            s_rs485_active = 0U;
            s_rs485_active_cmd_type = 0U;
            s_rs485.error_count++;
            real_board_hardware_publish_rs485();
        }

        memset(&s_rs485_queue[i], 0, sizeof(s_rs485_queue[i]));
        return;
    }
}

static void real_board_rs485_handle_timeout(void)
{
    if (s_rs485_active == 0U)
        return;

    if ((uint32_t)(g_real_board_millis - s_rs485_active_started_ms) < REAL_BOARD_RS485_RESPONSE_TIMEOUT_MS)
        return;

    s_rs485_active = 0U;
    s_rs485_active_cmd_type = 0U;
    s_rs485.valid = 1U;
    s_rs485.error_count++;
    real_board_hardware_publish_rs485();
}

uint8_t real_board_hardware_passthrough_a5(uint8_t cmd_type, const uint8_t *payload, uint16_t payload_len)
{
    return real_board_rs485_enqueue(cmd_type, payload, payload_len);
}

static void real_board_rs485_poll_environment(void)
{
    uint8_t query[8] = {0x01U, 0x04U, 0x00U, 0x01U, 0x00U, 0x02U, 0x00U, 0x00U};
    uint16_t crc = real_board_modbus_crc16(query, 6U);

    query[6] = (uint8_t)(crc & 0xFFU);
    query[7] = (uint8_t)(crc >> 8);
    real_board_rs485_enqueue(0x02U, query, sizeof(query));
}

static void real_board_rs485_handle_rx(void)
{
    uint8_t rx_buf[128];
    uint16_t len = usart2_read_available(rx_buf, sizeof(rx_buf));
    uint8_t crc_ok;

    if (len == 0U)
        return;

    s_rs485.valid = 1U;
    s_rs485.online = 1U;
    s_rs485.device_count = 1U;
    if (s_rs485_active != 0U)
    {
        uint32_t elapsed = g_real_board_millis - s_rs485_active_started_ms;
        s_rs485.last_response_ms = (elapsed > 0xFFFFUL) ? 0xFFFFU : (uint16_t)elapsed;
    }
    else
    {
        s_rs485.last_response_ms = REAL_BOARD_RS485_RESPONSE_MS;
    }
    s_rs485.rx_count++;

    crc_ok = real_board_modbus_crc_ok(rx_buf, len);
    if ((crc_ok == 0U) && ((s_rs485_active == 0U) || (s_rs485_active_cmd_type == 0x02U)))
    {
        s_rs485.error_count++;
    }

    if ((crc_ok != 0U) && (len >= 9U) && (rx_buf[0] == 0x01U) && (rx_buf[1] == 0x04U) && (rx_buf[2] >= 4U))
    {
        int16_t temp = (int16_t)read_be16(&rx_buf[3]);
        uint16_t hum = read_be16(&rx_buf[5]);

        s_environment.valid = 1U;
        s_environment.temperature_c_x10 = temp;
        s_environment.humidity_rh_x10 = hum;
        real_board_hardware_publish_environment();
    }

    if (s_rs485_active != 0U)
    {
        real_board_business_handle_peripheral_response(s_rs485_active_cmd_type, rx_buf, len);
        s_rs485_active = 0U;
        s_rs485_active_cmd_type = 0U;
    }
    real_board_hardware_publish_rs485();
}

static void real_board_rs485_task(void)
{
    real_board_rs485_handle_rx();
    real_board_rs485_handle_timeout();

    if ((uint32_t)(g_real_board_millis - s_rs485_tick) >= REAL_BOARD_RS485_PERIOD_TICKS)
    {
        s_rs485_tick = g_real_board_millis;
        real_board_rs485_poll_environment();
    }

    real_board_rs485_start_next();
}

void real_board_hardware_init(void)
{
    memset(&s_meter, 0, sizeof(s_meter));
    memset(&s_environment, 0, sizeof(s_environment));
    memset(&s_rs485, 0, sizeof(s_rs485));
    memset(s_rs485_queue, 0, sizeof(s_rs485_queue));
    s_rs485_active = 0U;
    s_rs485_active_cmd_type = 0U;
    s_rs485_active_started_ms = 0U;

    GPIO_SetPins(REAL_BOARD_RS485_EN_PORT, REAL_BOARD_RS485_EN_PIN);
    GPIO_ResetPins(REAL_BOARD_RE_485_1_PORT, REAL_BOARD_RE_485_1_PIN);

    usart2_init();
    usart3_init();

    real_board_hardware_publish_meter();
    real_board_hardware_publish_environment();
    real_board_hardware_publish_rs485();
    real_board_hlw8112_init_enable();
}

void real_board_hardware_task(void)
{
    real_board_hlw8112_handle_rx();
    real_board_hlw8112_init_task();
    real_board_hlw8112_read_task();
    real_board_hlw8112_handle_timeout();
    real_board_rs485_task();
}
