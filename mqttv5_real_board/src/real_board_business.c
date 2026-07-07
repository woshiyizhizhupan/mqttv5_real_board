#include "real_board_business.h"

#include "config.h"
#include "mbedtls/base64.h"
#include "real_board_lamp.h"
#include "real_board_ota.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#define REAL_BOARD_RAW_FRAME_MAX 1500U
#define REAL_BOARD_BASE64_MAX 1400U
#define REAL_BOARD_LEGACY_TEXT_MAX 96U
#define REAL_BOARD_A5_TIMEOUT_TICKS 300U
#define REAL_BOARD_LEGACY_SYNC_RESPONSE_CMD 0x81U
#define REAL_BOARD_LEGACY_HEARTBEAT_PAYLOAD_LEN 50U
#define REAL_BOARD_TIMER_CTL_MAX 50U
#define REAL_BOARD_TIMER_SEPARATOR_BYTES 4U

typedef struct
{
    uint8_t frm_type;
    uint8_t cmd_type;
    uint16_t payload_len;
    const uint8_t *payload;
} real_board_legacy_frame_t;

typedef struct
{
    uint8_t cmd_type;
    uint16_t payload_len;
    const uint8_t *payload;
} real_board_payload_frame_t;

typedef enum
{
    REAL_BOARD_A5_SLOT_EMPTY = 0,
    REAL_BOARD_A5_SLOT_PENDING,
    REAL_BOARD_A5_SLOT_RESPONSE,
    REAL_BOARD_A5_SLOT_TIMEOUT
} real_board_a5_slot_state_t;

typedef struct
{
    uint8_t used;
    uint8_t cmd_type;
    uint16_t request_len;
    uint16_t response_len;
    uint32_t request_tick;
    real_board_a5_slot_state_t state;
    uint8_t response[REAL_BOARD_A5_RESPONSE_MAX];
} real_board_a5_pending_t;

typedef struct
{
    uint8_t weeks;
    uint8_t hours;
    uint8_t minutes;
    uint8_t bright;
} real_board_timer_ctl_t;

typedef struct
{
    uint32_t uptime_ticks;
    uint16_t address;
    uint16_t keepalive_s;
    int16_t temperature_c_x10;
    uint16_t error_code;
    uint8_t error_renew;

    uint32_t voltage_mv;
    uint32_t current_ma;
    uint32_t active_power_mw;
    uint32_t reactive_power_mvar;
    uint32_t power_factor_x1000;
    uint32_t energy_one_wh;
    uint32_t energy_total_wh;
    uint32_t pulse_count;
    uint8_t meter_hlw8112_valid;

    int16_t environment_temperature_c_x10;
    uint16_t environment_humidity_rh_x10;
    uint16_t environment_pm25_ugm3;
    uint16_t environment_co2_ppm;
    uint32_t environment_illuminance_lux;
    uint8_t environment_valid;

    uint8_t rs485_online;
    uint8_t rs485_device_count;
    uint16_t rs485_last_response_ms;
    uint32_t rs485_tx_count;
    uint32_t rs485_rx_count;
    uint32_t rs485_error_count;
    uint8_t rs485_valid;

    uint8_t lamp1_brightness;
    uint8_t lamp2_brightness;
    uint8_t relay1_on;
    uint8_t relay2_on;
    uint32_t open_lamp_seconds;
    uint32_t bright_time_seconds;
    uint32_t bright_time_total_seconds;
    real_board_timer_ctl_t timer_ctl_1[REAL_BOARD_TIMER_CTL_MAX];
    real_board_timer_ctl_t timer_ctl_2[REAL_BOARD_TIMER_CTL_MAX];

    uint16_t rs485_read_period_s;
    uint16_t peripheral_type_code;
    uint8_t last_peripheral_cmd;
    uint16_t last_peripheral_payload_len;

    uint16_t test_load_power_w;
    uint16_t device_type;
    uint8_t reg_device_flag;
    uint8_t test_count;
    uint8_t test_results;
    uint8_t test_enable;

    char legacy_product_key[REAL_BOARD_LEGACY_TEXT_MAX];
    char legacy_device_name[REAL_BOARD_LEGACY_TEXT_MAX];
    char legacy_device_secret[REAL_BOARD_LEGACY_TEXT_MAX];
    char pending_event[32];
    char pending_event_level[12];
    uint32_t last_event_tick;
} real_board_state_t;

static real_board_state_t real_board_state;
static real_board_a5_pending_t a5_pending[REAL_BOARD_A5_PENDING_MAX];

__attribute__((weak)) uint8_t real_board_hardware_passthrough_a5(uint8_t cmd_type, const uint8_t *payload, uint16_t payload_len)
{
    (void)cmd_type;
    (void)payload;
    (void)payload_len;
    return 0U;
}

static uint32_t real_board_crc32(const uint8_t *buf, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0; i < size; i++)
    {
        crc ^= buf[i];
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 1UL)
                crc = (crc >> 1) ^ 0xEDB88320UL;
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

static uint32_t read_u32_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
}

static uint32_t read_u32_le(const uint8_t *buf)
{
    return ((uint32_t)buf[3] << 24) |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[1] << 8) |
           (uint32_t)buf[0];
}

static void write_u32_be(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)((value >> 24) & 0xFFU);
    buf[1] = (uint8_t)((value >> 16) & 0xFFU);
    buf[2] = (uint8_t)((value >> 8) & 0xFFU);
    buf[3] = (uint8_t)(value & 0xFFU);
}

static uint16_t read_u16_be(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
}

static void write_u16_be(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)((value >> 8) & 0xFFU);
    buf[1] = (uint8_t)(value & 0xFFU);
}

static void copy_text_field(char *dest, size_t dest_len, const uint8_t *src, uint16_t src_len)
{
    size_t copy_len = src_len;
    if (copy_len >= dest_len)
        copy_len = dest_len - 1U;
    memset(dest, 0, dest_len);
    memcpy(dest, src, copy_len);
}

static const char *find_legacy_frame_text(cJSON *root)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "frame");
    if (cJSON_IsString(item) && item->valuestring != NULL)
        return item->valuestring;

    item = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (cJSON_IsString(item) && item->valuestring != NULL)
        return item->valuestring;

    item = cJSON_GetObjectItemCaseSensitive(root, "msg");
    if (cJSON_IsString(item) && item->valuestring != NULL)
        return item->valuestring;

    item = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (cJSON_IsObject(item))
    {
        cJSON *frame = cJSON_GetObjectItemCaseSensitive(item, "frame");
        if (cJSON_IsString(frame) && frame->valuestring != NULL)
            return frame->valuestring;

        frame = cJSON_GetObjectItemCaseSensitive(item, "payload");
        if (cJSON_IsString(frame) && frame->valuestring != NULL)
            return frame->valuestring;

        frame = cJSON_GetObjectItemCaseSensitive(item, "msg");
        if (cJSON_IsString(frame) && frame->valuestring != NULL)
            return frame->valuestring;

        frame = cJSON_GetObjectItemCaseSensitive(item, "params");
        if (cJSON_IsString(frame) && frame->valuestring != NULL)
            return frame->valuestring;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsObject(item))
    {
        cJSON *frame = cJSON_GetObjectItemCaseSensitive(item, "frame");
        if (cJSON_IsString(frame) && frame->valuestring != NULL)
            return frame->valuestring;

        frame = cJSON_GetObjectItemCaseSensitive(item, "payload");
        if (cJSON_IsString(frame) && frame->valuestring != NULL)
            return frame->valuestring;

        frame = cJSON_GetObjectItemCaseSensitive(item, "msg");
        if (cJSON_IsString(frame) && frame->valuestring != NULL)
            return frame->valuestring;
    }

    return NULL;
}

static int hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static uint8_t is_hex_separator(char ch)
{
    return (ch == ' ') || (ch == '\r') || (ch == '\n') || (ch == '\t') ||
           (ch == '-') || (ch == ':') || (ch == ',');
}

static uint8_t legacy_text_looks_hex(const char *text)
{
    size_t digits = 0;

    if (text == NULL)
        return 0;

    while (*text != '\0')
    {
        if ((text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X')))
        {
            text += 2;
            continue;
        }
        if (is_hex_separator(*text))
        {
            text++;
            continue;
        }
        if (hex_nibble(*text) < 0)
            return 0;
        digits++;
        text++;
    }

    return (digits > 0U) && ((digits % 2U) == 0U);
}

static uint8_t decode_legacy_hex_frame(const char *text, uint8_t *out, size_t out_size, size_t *out_len)
{
    int high = -1;
    size_t len = 0;

    if (text == NULL)
        return 0;

    while (*text != '\0')
    {
        int value;

        if ((text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X')))
        {
            text += 2;
            continue;
        }
        if (is_hex_separator(*text))
        {
            text++;
            continue;
        }

        value = hex_nibble(*text);
        if (value < 0)
            return 0;
        if (high < 0)
        {
            high = value;
        }
        else
        {
            if (len >= out_size)
                return 0;
            out[len++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
        text++;
    }

    if (high >= 0)
        return 0;

    *out_len = len;
    return len > 0U ? 1U : 0U;
}

static uint8_t decode_legacy_frame(const char *text, uint8_t *out, size_t out_size, size_t *out_len)
{
    size_t olen = 0;

    if (text == NULL)
        return 0;

    if (legacy_text_looks_hex(text) && decode_legacy_hex_frame(text, out, out_size, out_len))
        return 1;

    if (mbedtls_base64_decode(out, out_size, &olen, (const unsigned char *)text, strlen(text)) == 0)
    {
        *out_len = olen;
        return 1;
    }

    if (!decode_legacy_hex_frame(text, out, out_size, out_len))
        return 0;

    return 1;
}

static uint8_t encode_legacy_frame(const uint8_t *frame, size_t frame_len, char *out, size_t out_size)
{
    size_t olen = 0;

    if (mbedtls_base64_encode((unsigned char *)out, out_size, &olen, frame, frame_len) != 0)
        return 0;

    if (olen >= out_size)
        return 0;

    out[olen] = '\0';
    return 1;
}

static uint8_t encode_legacy_frame_hex(const uint8_t *frame, size_t frame_len, char *out, size_t out_size)
{
    static const char hex[] = "0123456789ABCDEF";
    if (out_size < ((frame_len * 2U) + 1U))
        return 0;

    for (size_t i = 0; i < frame_len; i++)
    {
        out[i * 2U] = hex[(frame[i] >> 4) & 0x0FU];
        out[(i * 2U) + 1U] = hex[frame[i] & 0x0FU];
    }
    out[frame_len * 2U] = '\0';
    return 1;
}

static uint8_t parse_legacy_frame(const uint8_t *raw,
                                  size_t raw_len,
                                  real_board_legacy_frame_t *frame,
                                  char *error,
                                  size_t error_len)
{
    size_t offset = 0;
    size_t frame_start;
    size_t expected_len;
    uint32_t calc_crc;
    uint32_t crc_be;
    uint32_t crc_le;

    if (raw_len < 8U)
    {
        snprintf(error, error_len, "legacy frame is too short");
        return 0;
    }

    while ((offset < raw_len) && (raw[offset] == REAL_BOARD_FRM_PREAMBLE))
        offset++;

    if (offset == 0U)
    {
        snprintf(error, error_len, "legacy frame missing 0xFE preamble");
        return 0;
    }
    if ((raw_len - offset) < 8U)
    {
        snprintf(error, error_len, "legacy frame body is too short");
        return 0;
    }

    frame_start = offset;
    frame->frm_type = raw[offset++];
    frame->cmd_type = raw[offset++];
    frame->payload_len = ((uint16_t)raw[offset] << 8) | raw[offset + 1U];
    offset += 2U;
    expected_len = frame_start + 1U + 1U + 2U + frame->payload_len + 4U;

    if (expected_len != raw_len)
    {
        snprintf(error, error_len, "legacy frame length mismatch");
        return 0;
    }

    frame->payload = &raw[offset];
    calc_crc = real_board_crc32(&raw[frame_start], (uint32_t)(1U + 1U + 2U + frame->payload_len));
    crc_be = read_u32_be(&raw[raw_len - 4U]);
    crc_le = read_u32_le(&raw[raw_len - 4U]);
    if ((calc_crc != crc_be) && (calc_crc != crc_le))
    {
        snprintf(error, error_len, "legacy frame CRC mismatch");
        return 0;
    }

    return 1;
}

static uint8_t parse_legacy_payload_frame(const uint8_t *raw,
                                          size_t raw_len,
                                          real_board_payload_frame_t *frame,
                                          char *error,
                                          size_t error_len)
{
    uint16_t payload_len;
    size_t expected_len;
    uint32_t calc_crc;
    uint32_t crc_be;
    uint32_t crc_le;

    if ((raw == NULL) || (frame == NULL))
        return 0;
    if (raw_len < 8U)
    {
        snprintf(error, error_len, "legacy nested frame is too short");
        return 0;
    }
    if (raw[0] != REAL_BOARD_FRM_PREAMBLE)
    {
        snprintf(error, error_len, "legacy nested frame missing 0xFE preamble");
        return 0;
    }

    frame->cmd_type = raw[1];
    payload_len = read_u16_be(&raw[2]);
    expected_len = 1U + 1U + 2U + payload_len + 4U;
    if (expected_len != raw_len)
    {
        snprintf(error, error_len, "legacy nested frame length mismatch");
        return 0;
    }

    calc_crc = real_board_crc32(&raw[1], (uint32_t)(1U + 2U + payload_len));
    crc_be = read_u32_be(&raw[raw_len - 4U]);
    crc_le = read_u32_le(&raw[raw_len - 4U]);
    if ((calc_crc != crc_be) && (calc_crc != crc_le))
    {
        snprintf(error, error_len, "legacy nested frame CRC mismatch");
        return 0;
    }

    frame->payload_len = payload_len;
    frame->payload = &raw[4];
    return 1;
}

static size_t build_legacy_payload_frame(uint8_t cmd_type,
                                         const uint8_t *payload,
                                         uint16_t payload_len,
                                         uint8_t *out,
                                         size_t out_size)
{
    uint32_t crc;
    size_t cursor = 0;

    if (out_size < (size_t)(1U + 1U + 2U + payload_len + 4U))
        return 0;

    out[cursor++] = REAL_BOARD_FRM_PREAMBLE;
    out[cursor++] = cmd_type;
    out[cursor++] = (uint8_t)((payload_len >> 8) & 0xFFU);
    out[cursor++] = (uint8_t)(payload_len & 0xFFU);
    if (payload_len > 0U)
    {
        memcpy(&out[cursor], payload, payload_len);
        cursor += payload_len;
    }
    crc = real_board_crc32(&out[1], (uint32_t)(cursor - 1U));
    write_u32_be(&out[cursor], crc);
    cursor += 4U;
    return cursor;
}

static size_t build_legacy_response_frame(uint8_t cmd_type,
                                          const uint8_t *payload,
                                          uint16_t payload_len,
                                          uint8_t *out,
                                          size_t out_size)
{
    static const uint8_t legacy_ack_prefix[] = {0xFE, 0xA5};
    uint32_t crc;
    size_t cursor = 0;

    if (out_size < (size_t)(1U + 1U + 1U + 2U + payload_len + 4U))
        return 0;

    out[cursor++] = legacy_ack_prefix[0];
    out[cursor++] = legacy_ack_prefix[1];
    out[cursor++] = cmd_type;
    out[cursor++] = (uint8_t)((payload_len >> 8) & 0xFFU);
    out[cursor++] = (uint8_t)(payload_len & 0xFFU);
    if (payload_len > 0U)
    {
        memcpy(&out[cursor], payload, payload_len);
        cursor += payload_len;
    }
    crc = real_board_crc32(&out[1], (uint32_t)(cursor - 1U));
    write_u32_be(&out[cursor], crc);
    cursor += 4U;
    return cursor;
}

static cJSON *create_legacy_response_data(const real_board_legacy_frame_t *frame,
                                          const uint8_t *response_payload,
                                          uint16_t response_payload_len,
                                          const char *action)
{
    uint8_t response_frame[REAL_BOARD_RAW_FRAME_MAX];
    char response_base64[REAL_BOARD_BASE64_MAX];
    char response_hex[(REAL_BOARD_RAW_FRAME_MAX * 2U) + 1U];
    size_t response_len;
    cJSON *data = cJSON_CreateObject();

    if (data == NULL)
        return NULL;

    response_len = build_legacy_response_frame(REAL_BOARD_LEGACY_SYNC_RESPONSE_CMD,
                                               response_payload,
                                               response_payload_len,
                                               response_frame,
                                               sizeof(response_frame));
    cJSON_AddBoolToObject(data, "legacy_protocol", 1);
    cJSON_AddNumberToObject(data, "frame_type", frame->frm_type);
    cJSON_AddNumberToObject(data, "cmd_type", frame->cmd_type);
    cJSON_AddStringToObject(data, "action", action);
    if ((response_len > 0U) && encode_legacy_frame(response_frame, response_len, response_base64, sizeof(response_base64)))
    {
        cJSON_AddStringToObject(data, "frame", response_base64);
        cJSON_AddStringToObject(data, "msg", response_base64);
    }
    else
    {
        cJSON_AddStringToObject(data, "frame", "");
        cJSON_AddStringToObject(data, "msg", "");
    }
    if ((response_len > 0U) && encode_legacy_frame_hex(response_frame, response_len, response_hex, sizeof(response_hex)))
    {
        cJSON_AddStringToObject(data, "frame_hex", response_hex);
        cJSON_AddStringToObject(data, "msg_hex", response_hex);
    }
    else
    {
        cJSON_AddStringToObject(data, "frame_hex", "");
        cJSON_AddStringToObject(data, "msg_hex", "");
    }

    return data;
}

static uint8_t create_legacy_received_data(const real_board_legacy_frame_t *frame,
                                            cJSON **response_data,
                                            int *code,
                                            const char **message)
{
    uint8_t response_payload[1] = {0x00U};

    *code = 0;
    *message = "legacy frame received";
    *response_data = create_legacy_response_data(frame, response_payload, sizeof(response_payload), "received");
    return 1;
}

static const char *a5_state_text(real_board_a5_slot_state_t state)
{
    switch (state)
    {
    case REAL_BOARD_A5_SLOT_PENDING:
        return "pending";
    case REAL_BOARD_A5_SLOT_RESPONSE:
        return "response";
    case REAL_BOARD_A5_SLOT_TIMEOUT:
        return "timeout";
    default:
        return "empty";
    }
}

static int find_a5_slot(uint8_t cmd_type, uint8_t pending_only)
{
    int reusable = -1;

    for (uint8_t i = 0; i < REAL_BOARD_A5_PENDING_MAX; i++)
    {
        if (a5_pending[i].used &&
            a5_pending[i].cmd_type == cmd_type &&
            (!pending_only || a5_pending[i].state == REAL_BOARD_A5_SLOT_PENDING))
        {
            return (int)i;
        }
        if (reusable < 0 &&
            (!a5_pending[i].used || a5_pending[i].state != REAL_BOARD_A5_SLOT_PENDING))
        {
            reusable = (int)i;
        }
    }

    return pending_only ? -1 : reusable;
}

static int queue_a5_request(const real_board_legacy_frame_t *frame)
{
    int slot = find_a5_slot(frame->cmd_type, 0);
    if (slot < 0)
        return -1;

    memset(&a5_pending[slot], 0, sizeof(a5_pending[slot]));
    a5_pending[slot].used = 1U;
    a5_pending[slot].cmd_type = frame->cmd_type;
    a5_pending[slot].request_len = frame->payload_len;
    a5_pending[slot].request_tick = real_board_state.uptime_ticks;
    a5_pending[slot].state = REAL_BOARD_A5_SLOT_PENDING;
    return slot;
}

static void append_a5_pending(cJSON *object)
{
    cJSON *array = cJSON_AddArrayToObject(object, "a5_pending");
    if (array == NULL)
        return;

    for (uint8_t i = 0; i < REAL_BOARD_A5_PENDING_MAX; i++)
    {
        cJSON *item;
        if (!a5_pending[i].used)
            continue;

        item = cJSON_CreateObject();
        if (item == NULL)
            continue;
        cJSON_AddNumberToObject(item, "slot", i);
        cJSON_AddNumberToObject(item, "cmd_type", a5_pending[i].cmd_type);
        cJSON_AddStringToObject(item, "state", a5_state_text(a5_pending[i].state));
        cJSON_AddNumberToObject(item, "request_len", a5_pending[i].request_len);
        cJSON_AddNumberToObject(item, "response_len", a5_pending[i].response_len);
        cJSON_AddItemToArray(array, item);
    }
}

static void append_peripheral(cJSON *array, const char *name, uint8_t legacy_cmd)
{
    cJSON *item = cJSON_CreateObject();
    if (item == NULL)
        return;

    cJSON_AddStringToObject(item, "name", name);
    cJSON_AddNumberToObject(item, "legacy_cmd", legacy_cmd);
    cJSON_AddBoolToObject(item, "configured", (real_board_state.peripheral_type_code != 0U));
    cJSON_AddBoolToObject(item, "last_downlink", real_board_state.last_peripheral_cmd == legacy_cmd);
    cJSON_AddBoolToObject(item, "pending", find_a5_slot(legacy_cmd, 1) >= 0);
    cJSON_AddItemToArray(array, item);
}

static uint8_t json_optional_u8(cJSON *params, const char *name, uint8_t *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(params, name);
    if (item == NULL)
        return 1;
    if (!cJSON_IsNumber(item) || item->valueint < 0 || item->valueint > 255)
        return 0;
    *value = (uint8_t)item->valueint;
    return 1;
}

static void queue_event(const char *event_name, const char *level)
{
    snprintf(real_board_state.pending_event, sizeof(real_board_state.pending_event), "%s", event_name);
    snprintf(real_board_state.pending_event_level, sizeof(real_board_state.pending_event_level), "%s", level);
}

static void append_event_payload(cJSON *root, const char *event_name, const char *level)
{
    cJSON *data;

    cJSON_AddStringToObject(root, "schema", "emqx-gateway.realboard.event.v1");
    cJSON_AddStringToObject(root, "device_id", system_config->device_id);
    cJSON_AddStringToObject(root, "firmware_version", REAL_BOARD_FW_VERSION);
    cJSON_AddStringToObject(root, "business_mode", "real_board");
    cJSON_AddStringToObject(root, "event", event_name);
    cJSON_AddStringToObject(root, "level", level);
    cJSON_AddNumberToObject(root, "uptime_ticks", real_board_state.uptime_ticks);

    data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddBoolToObject(data, "meter_hlw8112_valid", real_board_state.meter_hlw8112_valid);
    cJSON_AddBoolToObject(data, "environment_valid", real_board_state.environment_valid);
    cJSON_AddBoolToObject(data, "rs485_valid", real_board_state.rs485_valid);
    cJSON_AddBoolToObject(data, "rs485_online", real_board_state.rs485_online);
    cJSON_AddNumberToObject(data, "error_code", real_board_state.error_code);
}

static uint8_t parse_host_port_payload(const uint8_t *payload, uint16_t payload_len, char *host, size_t host_len, uint16_t *port)
{
    char text[MQTT_HOST_LEN + 12U];
    char *comma;
    unsigned long parsed_port;
    size_t copy_len = payload_len;

    if (copy_len >= sizeof(text))
        copy_len = sizeof(text) - 1U;

    memset(text, 0, sizeof(text));
    memcpy(text, payload, copy_len);

    comma = strchr(text, ',');
    if (comma == NULL)
        return 0;
    *comma = '\0';
    parsed_port = strtoul(comma + 1, NULL, 10);
    if ((text[0] == '\0') || (strlen(text) >= host_len) || (parsed_port == 0UL) || (parsed_port > 65535UL))
        return 0;

    memset(host, 0, host_len);
    strcpy(host, text);
    *port = (uint16_t)parsed_port;
    return 1;
}

static void make_cpu_id_payload(uint8_t *payload, uint16_t *payload_len)
{
    uint32_t hash = real_board_crc32((const uint8_t *)system_config->device_id, (uint32_t)strlen(system_config->device_id));

    memset(payload, 0, 13U);
    payload[0] = (uint8_t)((hash >> 24) & 0xFFU);
    payload[1] = (uint8_t)((hash >> 16) & 0xFFU);
    payload[2] = (uint8_t)((hash >> 8) & 0xFFU);
    payload[3] = (uint8_t)(hash & 0xFFU);
    memcpy(&payload[4], system_config->eth.mac, 6U);
    payload[10] = 0x40U;
    payload[11] = 0x05U;
    payload[12] = 0x00U;
    *payload_len = 13U;
}

static void save_current_config(uint8_t *reboot_requested)
{
    config_save(&system_config_temp);
    *reboot_requested = 1U;
}

static uint8_t handle_a1_command(const real_board_legacy_frame_t *frame,
                                 cJSON **response_data,
                                 int *code,
                                 const char **message,
                                 uint8_t *reboot_requested)
{
    uint8_t response_payload[320];
    uint16_t response_len = 0;
    char host[MQTT_HOST_LEN];
    uint16_t port = 0;

    memset(response_payload, 0, sizeof(response_payload));

    switch (frame->cmd_type)
    {
    case REAL_BOARD_CMD_A1_READ_CPU_ID:
        make_cpu_id_payload(response_payload, &response_len);
        *message = "legacy CPU ID returned";
        break;

    case REAL_BOARD_CMD_A1_SET_PRODUCT_KEY:
        copy_text_field(real_board_state.legacy_product_key, sizeof(real_board_state.legacy_product_key), frame->payload, frame->payload_len);
        copy_text_field((char *)response_payload, sizeof(response_payload), frame->payload, frame->payload_len);
        response_len = (uint16_t)strlen((char *)response_payload);
        *message = "legacy product key accepted";
        break;

    case REAL_BOARD_CMD_A1_SET_DEVICE_NAME:
        copy_text_field(real_board_state.legacy_device_name, sizeof(real_board_state.legacy_device_name), frame->payload, frame->payload_len);
        copy_text_field(system_config_temp.device_id, sizeof(system_config_temp.device_id), frame->payload, frame->payload_len);
        config_save(&system_config_temp);
        *reboot_requested = 1U;
        copy_text_field((char *)response_payload, sizeof(response_payload), frame->payload, frame->payload_len);
        response_len = (uint16_t)strlen((char *)response_payload);
        *message = "legacy device name mapped to device_id";
        break;

    case REAL_BOARD_CMD_A1_SET_DEVICE_SECRET:
        copy_text_field(real_board_state.legacy_device_secret, sizeof(real_board_state.legacy_device_secret), frame->payload, frame->payload_len);
        response_payload[0] = 0x00U;
        response_len = 1U;
        *message = "legacy device secret accepted in runtime state";
        break;

    case REAL_BOARD_CMD_A1_SET_ENDPOINT:
        if (!parse_host_port_payload(frame->payload, frame->payload_len, host, sizeof(host), &port))
        {
            *code = 422;
            *message = "legacy endpoint payload must be host,port";
            return 1;
        }
        memset(system_config_temp.mqtt_server.host, 0, sizeof(system_config_temp.mqtt_server.host));
        strcpy(system_config_temp.mqtt_server.host, host);
        system_config_temp.mqtt_server.port = port;
        save_current_config(reboot_requested);
        response_payload[0] = 0x00U;
        response_len = 1U;
        *message = "legacy endpoint saved";
        break;

    case REAL_BOARD_CMD_A1_SET_KNS:
        copy_text_field(real_board_state.legacy_product_key, sizeof(real_board_state.legacy_product_key), frame->payload, frame->payload_len);
        response_payload[0] = 0x00U;
        response_len = 1U;
        *message = "legacy ProductKey/DeviceName/Secret payload accepted";
        break;

    default:
        return create_legacy_received_data(frame, response_data, code, message);
    }

    *response_data = create_legacy_response_data(frame, response_payload, response_len, "mainboard_parameter");
    return 1;
}

static uint8_t handle_a2_command(const real_board_legacy_frame_t *frame,
                                 cJSON **response_data,
                                 int *code,
                                 const char **message,
                                 uint8_t *reboot_requested)
{
    uint8_t response_payload[4] = {0};
    const uint8_t value = (frame->payload_len > 0U) ? frame->payload[0] : 1U;

    switch (frame->cmd_type)
    {
    case REAL_BOARD_CMD_A2_RESET:
        *reboot_requested = 1U;
        *message = "legacy reset accepted";
        break;
    case 0x02U:
        real_board_state.relay1_on = value ? 1U : 0U;
        real_board_state.relay2_on = value ? 1U : 0U;
        *message = "legacy all device power accepted";
        break;
    case 0x03U:
        real_board_state.relay1_on = value ? 1U : 0U;
        real_board_state.lamp1_brightness = value ? 100U : 0U;
        *message = "legacy lamp power accepted";
        break;
    case 0x0DU:
        real_board_state.relay1_on = value ? 1U : 0U;
        *message = "legacy relay1 accepted";
        break;
    case 0x0EU:
        real_board_state.relay2_on = value ? 1U : 0U;
        *message = "legacy relay2 accepted";
        break;
    default:
        return create_legacy_received_data(frame, response_data, code, message);
    }

    *response_data = create_legacy_response_data(frame, response_payload, 1U, "mainboard_control");
    return 1;
}

static uint16_t clamp_u32_to_u16(uint32_t value)
{
    return value > 0xFFFFU ? 0xFFFFU : (uint16_t)value;
}

static uint8_t real_board_set_lamp_brightness(uint16_t address, uint8_t brightness)
{
    if ((address != 0x0001U) && (address != 0x0002U) && (address != 0xFFFFU))
        return 1U;

    if (brightness > 100U)
        brightness = 100U;

    if ((address == 0x0001U) || (address == 0xFFFFU))
    {
        Adjust_Lamp(brightness);
        real_board_state.lamp1_brightness = brightness;
        real_board_state.relay1_on = brightness ? 1U : 0U;
    }
    if ((address == 0x0002U) || (address == 0xFFFFU))
    {
        Adjust_UNIT4_Lamp(brightness);
        real_board_state.lamp2_brightness = brightness;
        real_board_state.relay2_on = brightness ? 1U : 0U;
    }

    return 0U;
}

static uint8_t write_legacy_heartbeat_payload(uint16_t address,
                                              uint8_t *response_payload,
                                              uint16_t response_payload_size,
                                              uint16_t *response_payload_len,
                                              int *code,
                                              const char **message)
{
    uint16_t cursor = 0U;
    uint32_t bright_time = real_board_state.bright_time_seconds;
    uint32_t bright_time_total = real_board_state.bright_time_total_seconds;
    uint32_t electricity_one = real_board_state.energy_one_wh;
    const uint8_t any_lamp_on = (real_board_state.lamp1_brightness > 0U) ||
                                (real_board_state.lamp2_brightness > 0U);

    if (response_payload_size < REAL_BOARD_LEGACY_HEARTBEAT_PAYLOAD_LEN)
    {
        *code = 500;
        *message = "legacy heartbeat response buffer is too small";
        return 1;
    }

    if (any_lamp_on && bright_time == 0U)
        bright_time = real_board_state.open_lamp_seconds;

    write_u16_be(&response_payload[cursor], address);
    cursor += 2U;
    response_payload[cursor++] = 26U;
    response_payload[cursor++] = 7U;
    response_payload[cursor++] = 2U;
    response_payload[cursor++] = (uint8_t)((real_board_state.uptime_ticks / 3600U) % 24U);
    response_payload[cursor++] = (uint8_t)((real_board_state.uptime_ticks / 60U) % 60U);
    response_payload[cursor++] = (uint8_t)(real_board_state.uptime_ticks % 60U);
    write_u16_be(&response_payload[cursor], (uint16_t)real_board_state.temperature_c_x10);
    cursor += 2U;
    response_payload[cursor++] = real_board_state.lamp1_brightness;
    write_u16_be(&response_payload[cursor], clamp_u32_to_u16(real_board_state.voltage_mv / 100U));
    cursor += 2U;
    write_u16_be(&response_payload[cursor], clamp_u32_to_u16(real_board_state.current_ma));
    cursor += 2U;
    response_payload[cursor++] = 0U;
    response_payload[cursor++] = 50U;
    write_u16_be(&response_payload[cursor], clamp_u32_to_u16(real_board_state.power_factor_x1000));
    cursor += 2U;
    write_u16_be(&response_payload[cursor], clamp_u32_to_u16(real_board_state.active_power_mw / 1000U));
    cursor += 2U;
    write_u16_be(&response_payload[cursor], real_board_state.test_load_power_w);
    cursor += 2U;
    write_u32_be(&response_payload[cursor], bright_time);
    cursor += 4U;
    write_u32_be(&response_payload[cursor], bright_time_total);
    cursor += 4U;
    write_u32_be(&response_payload[cursor], electricity_one);
    cursor += 4U;
    write_u32_be(&response_payload[cursor], real_board_state.energy_total_wh);
    cursor += 4U;
    response_payload[cursor++] = real_board_state.lamp2_brightness;
    memset(&response_payload[cursor], 0, 10U);
    cursor += 10U;

    *response_payload_len = cursor;
    *message = "legacy nested heartbeat returned";
    return 1;
}

static uint8_t append_timer_list(const real_board_timer_ctl_t *list,
                                 uint8_t *response_payload,
                                 uint16_t response_payload_size,
                                 uint16_t *cursor)
{
    for (uint16_t i = 0U; i < REAL_BOARD_TIMER_CTL_MAX; i++)
    {
        if (list[i].weeks == 0xFFU)
            break;
        if ((*cursor + 4U) > response_payload_size)
            return 0;
        response_payload[(*cursor)++] = list[i].weeks;
        response_payload[(*cursor)++] = list[i].hours;
        response_payload[(*cursor)++] = list[i].minutes;
        response_payload[(*cursor)++] = list[i].bright;
    }

    return 1;
}

static uint8_t write_legacy_timer_payload(uint16_t address,
                                          uint8_t *response_payload,
                                          uint16_t response_payload_size,
                                          uint16_t *response_payload_len,
                                          int *code,
                                          const char **message)
{
    uint16_t cursor = 0U;

    if (response_payload_size < (2U + REAL_BOARD_TIMER_SEPARATOR_BYTES))
    {
        *code = 500;
        *message = "legacy timer response buffer is too small";
        return 1;
    }

    write_u16_be(&response_payload[cursor], address);
    cursor += 2U;
    if (!append_timer_list(real_board_state.timer_ctl_1, response_payload, response_payload_size, &cursor))
    {
        *code = 500;
        *message = "legacy timer lamp1 response overflow";
        return 1;
    }
    if ((cursor + REAL_BOARD_TIMER_SEPARATOR_BYTES) > response_payload_size)
    {
        *code = 500;
        *message = "legacy timer separator response overflow";
        return 1;
    }
    for (uint8_t i = 0U; i < REAL_BOARD_TIMER_SEPARATOR_BYTES; i++)
        response_payload[cursor++] = 0xCCU;
    if (!append_timer_list(real_board_state.timer_ctl_2, response_payload, response_payload_size, &cursor))
    {
        *code = 500;
        *message = "legacy timer lamp2 response overflow";
        return 1;
    }

    *response_payload_len = cursor;
    *message = "legacy nested timer setting returned";
    return 1;
}

static void clear_timer_list(real_board_timer_ctl_t *list)
{
    memset(list, 0xFF, sizeof(real_board_timer_ctl_t) * REAL_BOARD_TIMER_CTL_MAX);
}

static uint8_t store_timer_list(real_board_timer_ctl_t *list, const uint8_t *payload, uint16_t payload_len)
{
    uint16_t item_count;

    if ((payload_len % 4U) != 0U)
        return 0;
    item_count = (uint16_t)(payload_len / 4U);
    if (item_count > REAL_BOARD_TIMER_CTL_MAX)
        return 0;

    clear_timer_list(list);
    for (uint16_t i = 0U; i < item_count; i++)
    {
        list[i].weeks = payload[(i * 4U)];
        list[i].hours = payload[(i * 4U) + 1U];
        list[i].minutes = payload[(i * 4U) + 2U];
        list[i].bright = payload[(i * 4U) + 3U];
        if (list[i].bright > 100U)
            list[i].bright = 100U;
    }

    return 1;
}

static uint8_t store_legacy_timer_payload(const real_board_payload_frame_t *nested_frame,
                                          uint16_t *address_out)
{
    uint16_t address;
    const uint8_t *timer_payload;
    uint16_t timer_payload_len;

    if ((nested_frame->payload == NULL) || (nested_frame->payload_len < 2U))
        return 0;

    address = read_u16_be(nested_frame->payload);
    timer_payload = &nested_frame->payload[2];
    timer_payload_len = (uint16_t)(nested_frame->payload_len - 2U);
    *address_out = address;

    switch (address)
    {
    case 0xFFFFU:
        if (!store_timer_list(real_board_state.timer_ctl_1, timer_payload, timer_payload_len))
            return 0;
        if (!store_timer_list(real_board_state.timer_ctl_2, timer_payload, timer_payload_len))
            return 0;
        break;
    case 0x0001U:
        if (!store_timer_list(real_board_state.timer_ctl_1, timer_payload, timer_payload_len))
            return 0;
        break;
    case 0x0002U:
        if (!store_timer_list(real_board_state.timer_ctl_2, timer_payload, timer_payload_len))
            return 0;
        break;
    default:
        return 0;
    }

    return 1;
}

static uint8_t handle_nested_lamp_control(const real_board_payload_frame_t *nested_frame,
                                          uint8_t *response_payload,
                                          uint16_t response_payload_size,
                                          uint16_t *response_payload_len,
                                          int *code,
                                          const char **message)
{
    uint16_t address;
    uint8_t brightness;
    uint8_t status = 0x00U;

    if ((nested_frame->cmd_type != 0x01U) ||
        (nested_frame->payload == NULL) ||
        (nested_frame->payload_len != 3U) ||
        (response_payload_size < 3U))
        return 0;

    address = read_u16_be(nested_frame->payload);
    brightness = nested_frame->payload[2];
    if (brightness > 100U)
    {
        brightness = 100U;
        status = 0x01U;
        *code = 422;
    }

    if (real_board_set_lamp_brightness(address, brightness) != 0U)
        status = 0x01U;

    response_payload[0] = nested_frame->payload[0];
    response_payload[1] = nested_frame->payload[1];
    response_payload[2] = status;
    *response_payload_len = 3U;
    *message = status == 0U ? "legacy nested lamp control accepted" : "legacy nested lamp brightness clipped";
    return 1;
}

static uint16_t nested_request_address(const real_board_payload_frame_t *nested_frame)
{
    if ((nested_frame->payload != NULL) && (nested_frame->payload_len >= 2U))
        return read_u16_be(nested_frame->payload);
    return real_board_state.address;
}

static uint8_t write_nested_address_status(uint8_t *response_payload,
                                           uint16_t response_payload_size,
                                           uint16_t address,
                                           uint8_t status,
                                           uint16_t *response_payload_len,
                                           int *code,
                                           const char **message)
{
    if (response_payload_size < 3U)
    {
        *code = 500;
        *message = "legacy nested response buffer is too small";
        return 1;
    }

    write_u16_be(response_payload, address);
    response_payload[2] = status;
    *response_payload_len = 3U;
    if (status != 0U && *code == 0)
        *code = 422;
    return 1;
}

static uint8_t handle_nested_read_info(const real_board_payload_frame_t *nested_frame,
                                       uint8_t *response_payload,
                                       uint16_t response_payload_size,
                                       uint16_t *response_payload_len,
                                       int *code,
                                       const char **message)
{
    uint16_t address = nested_request_address(nested_frame);
    uint32_t serial;

    switch (nested_frame->cmd_type)
    {
    case 0x02U:
        return write_legacy_heartbeat_payload(address,
                                              response_payload,
                                              response_payload_size,
                                              response_payload_len,
                                              code,
                                              message);

    case 0x10U:
        if (response_payload_size < 6U)
        {
            *code = 500;
            *message = "legacy version response buffer is too small";
            return 1;
        }
        write_u16_be(&response_payload[0], address);
        response_payload[2] = 1U;
        response_payload[3] = 10U;
        response_payload[4] = 2U;
        response_payload[5] = 55U;
        *response_payload_len = 6U;
        *message = "legacy nested version returned";
        return 1;

    case 0x11U:
        if (response_payload_size < 4U)
        {
            *code = 500;
            *message = "legacy keepalive response buffer is too small";
            return 1;
        }
        write_u16_be(&response_payload[0], address);
        write_u16_be(&response_payload[2], real_board_state.keepalive_s);
        *response_payload_len = 4U;
        *message = "legacy nested keepalive returned";
        return 1;

    case 0x14U:
        return write_legacy_timer_payload(address,
                                          response_payload,
                                          response_payload_size,
                                          response_payload_len,
                                          code,
                                          message);

    case 0x15U:
        if (response_payload_size < 6U)
        {
            *code = 500;
            *message = "legacy identify response buffer is too small";
            return 1;
        }
        serial = real_board_crc32((const uint8_t *)system_config->device_id,
                                  (uint32_t)strlen(system_config->device_id));
        write_u16_be(&response_payload[0], address);
        write_u32_be(&response_payload[2], serial);
        *response_payload_len = 6U;
        *message = "legacy nested factory serial returned";
        return 1;

    default:
        return 0;
    }
}

static uint8_t handle_nested_set_command(const real_board_payload_frame_t *nested_frame,
                                         uint8_t *response_payload,
                                         uint16_t response_payload_size,
                                         uint16_t *response_payload_len,
                                         int *code,
                                         const char **message,
                                         uint8_t *reboot_requested)
{
    uint16_t address = nested_request_address(nested_frame);
    uint8_t status = 0U;

    switch (nested_frame->cmd_type)
    {
    case 0x20U:
        if ((nested_frame->payload == NULL) || (nested_frame->payload_len < 4U))
        {
            status = 1U;
            *message = "legacy nested set address payload invalid";
        }
        else
        {
            real_board_state.address = read_u16_be(&nested_frame->payload[2]);
            address = real_board_state.address;
            *message = "legacy nested set address accepted";
        }
        return write_nested_address_status(response_payload, response_payload_size, address, status,
                                           response_payload_len, code, message);

    case 0x21U:
        if ((nested_frame->payload == NULL) || (nested_frame->payload_len < 4U))
        {
            status = 1U;
            *message = "legacy nested set keepalive payload invalid";
        }
        else
        {
            real_board_state.keepalive_s = read_u16_be(&nested_frame->payload[2]);
            *message = "legacy nested set keepalive accepted";
        }
        return write_nested_address_status(response_payload, response_payload_size, address, status,
                                           response_payload_len, code, message);

    case 0x22U:
        if ((nested_frame->payload == NULL) || (nested_frame->payload_len < 4U))
        {
            status = 1U;
            *message = "legacy nested set power constant payload invalid";
        }
        else
        {
            real_board_state.test_load_power_w = read_u16_be(&nested_frame->payload[2]);
            *message = "legacy nested set power constant accepted";
        }
        return write_nested_address_status(response_payload, response_payload_size, address, status,
                                           response_payload_len, code, message);

    case 0x23U:
        if (!store_legacy_timer_payload(nested_frame, &address))
        {
            status = 1U;
            *message = "legacy nested set timer payload invalid";
        }
        else
        {
            *message = "legacy nested set timer accepted";
        }
        return write_nested_address_status(response_payload, response_payload_size, address, status,
                                           response_payload_len, code, message);

    case 0x25U:
        real_board_state.address = 1U;
        real_board_state.keepalive_s = 300U;
        real_board_state.error_code = 0U;
        real_board_state.error_renew = 0U;
        real_board_state.lamp1_brightness = 0U;
        real_board_state.lamp2_brightness = 0U;
        real_board_state.relay1_on = 0U;
        real_board_state.relay2_on = 0U;
        clear_timer_list(real_board_state.timer_ctl_1);
        clear_timer_list(real_board_state.timer_ctl_2);
        *message = "legacy nested restore defaults accepted";
        return write_nested_address_status(response_payload, response_payload_size, real_board_state.address, status,
                                           response_payload_len, code, message);

    case 0x26U:
        *reboot_requested = 1U;
        *message = "legacy nested reset accepted";
        return write_nested_address_status(response_payload, response_payload_size, address, status,
                                           response_payload_len, code, message);

    case 0x28U:
        *message = "legacy nested RTC set accepted";
        return write_nested_address_status(response_payload, response_payload_size, address, status,
                                           response_payload_len, code, message);

    case 0x29U:
        real_board_state.error_code = 0U;
        real_board_state.error_renew = 0U;
        *message = "legacy nested error clear accepted";
        return write_nested_address_status(response_payload, response_payload_size, address, status,
                                           response_payload_len, code, message);

    case 0x2AU:
        if ((nested_frame->payload == NULL) || (nested_frame->payload_len < 4U))
        {
            status = 1U;
            *message = "legacy nested threshold payload invalid";
        }
        else
        {
            real_board_state.test_load_power_w = read_u16_be(&nested_frame->payload[2]);
            *message = "legacy nested threshold accepted";
        }
        return write_nested_address_status(response_payload, response_payload_size, address, status,
                                           response_payload_len, code, message);

    default:
        return 0;
    }
}

static uint8_t handle_nested_v3_command(const real_board_payload_frame_t *nested_frame,
                                        uint8_t *response_payload,
                                        uint16_t response_payload_size,
                                        uint16_t *response_payload_len,
                                        int *code,
                                        const char **message,
                                        uint8_t *reboot_requested)
{
    if (nested_frame->cmd_type == 0x01U)
    {
        if (handle_nested_lamp_control(nested_frame,
                                       response_payload,
                                       response_payload_size,
                                       response_payload_len,
                                       code,
                                       message))
        {
            return 1;
        }
        *code = 422;
        *message = "legacy nested lamp control payload invalid";
        return write_nested_address_status(response_payload,
                                           response_payload_size,
                                           nested_request_address(nested_frame),
                                           1U,
                                           response_payload_len,
                                           code,
                                           message);
    }

    if (handle_nested_read_info(nested_frame,
                                response_payload,
                                response_payload_size,
                                response_payload_len,
                                code,
                                message))
    {
        return 1;
    }

    return handle_nested_set_command(nested_frame,
                                     response_payload,
                                     response_payload_size,
                                     response_payload_len,
                                     code,
                                     message,
                                     reboot_requested);
}

static uint8_t handle_nested_lamp_received(const real_board_payload_frame_t *nested_frame,
                                           uint8_t *response_payload,
                                           uint16_t response_payload_size,
                                           uint16_t *response_payload_len,
                                           int *code,
                                           const char **message)
{
    (void)nested_frame;
    if (response_payload_size < 1U)
    {
        *code = 500;
        *message = "legacy nested response buffer is too small";
        return 1;
    }

    response_payload[0] = 0x00U;
    *response_payload_len = 1U;
    *message = "legacy nested payload received";
    return 1;
}

static uint8_t handle_a5_nested_payload_frame(const real_board_legacy_frame_t *frame,
                                              cJSON **response_data,
                                              int *code,
                                              const char **message,
                                              uint8_t *reboot_requested)
{
    real_board_payload_frame_t nested_frame;
    uint8_t nested_response_payload[REAL_BOARD_RAW_FRAME_MAX];
    uint8_t nested_response_frame[REAL_BOARD_RAW_FRAME_MAX];
    uint16_t nested_response_payload_len = 0U;
    size_t nested_response_frame_len;
    char error[96];
    const char *action = "nested_payload";

    if ((frame->payload == NULL) || (frame->payload_len == 0U) || (frame->payload[0] != REAL_BOARD_FRM_PREAMBLE))
        return 0;

    if (!parse_legacy_payload_frame(frame->payload, frame->payload_len, &nested_frame, error, sizeof(error)))
    {
        uint8_t parse_error_payload[1] = {0x00U};
        *code = 422;
        *message = "legacy nested frame parse failed";
        *response_data = create_legacy_response_data(frame, parse_error_payload, sizeof(parse_error_payload), "nested_parse_error");
        if (*response_data != NULL)
            cJSON_AddStringToObject(*response_data, "error", error);
        return 1;
    }

    memset(nested_response_payload, 0, sizeof(nested_response_payload));
    *code = 0;
    *message = "ok";
    *reboot_requested = 0U;

    if ((nested_frame.cmd_type & 0xF0U) == 0xB0U)
    {
        if (!real_board_ota_handle_legacy_payload(nested_frame.cmd_type,
                                                  nested_frame.payload,
                                                  nested_frame.payload_len,
                                                  nested_response_payload,
                                                  sizeof(nested_response_payload),
                                                  &nested_response_payload_len,
                                                  code,
                                                  message,
                                                  reboot_requested))
        {
            nested_response_payload[0] = 0x00U;
            nested_response_payload_len = 1U;
            *code = 404;
            *message = "legacy nested OTA command is unsupported";
        }
        if (nested_response_payload_len == 0U)
        {
            nested_response_payload[0] = (*code == 0) ? 0x01U : 0x00U;
            nested_response_payload_len = 1U;
        }
        action = "legacy_nested_ota";
    }
    else if (handle_nested_v3_command(&nested_frame,
                                      nested_response_payload,
                                      sizeof(nested_response_payload),
                                      &nested_response_payload_len,
                                      code,
                                      message,
                                      reboot_requested))
    {
        action = "legacy_nested_v3";
    }
    else
    {
        handle_nested_lamp_received(&nested_frame,
                                    nested_response_payload,
                                    sizeof(nested_response_payload),
                                    &nested_response_payload_len,
                                    code,
                                    message);
        action = "legacy_nested_received";
    }

    nested_response_frame_len = build_legacy_payload_frame(nested_frame.cmd_type,
                                                           nested_response_payload,
                                                           nested_response_payload_len,
                                                           nested_response_frame,
                                                           sizeof(nested_response_frame));
    if (nested_response_frame_len == 0U || nested_response_frame_len > 0xFFFFU)
    {
        uint8_t error_payload[1] = {0x00U};
        *code = 500;
        *message = "legacy nested response frame build failed";
        *response_data = create_legacy_response_data(frame, error_payload, sizeof(error_payload), "nested_response_error");
        return 1;
    }

    *response_data = create_legacy_response_data(frame,
                                                 nested_response_frame,
                                                 (uint16_t)nested_response_frame_len,
                                                 action);
    if (*response_data != NULL)
    {
        cJSON_AddNumberToObject(*response_data, "nested_cmd_type", nested_frame.cmd_type);
        cJSON_AddNumberToObject(*response_data, "nested_payload_len", nested_frame.payload_len);
    }
    return 1;
}

static uint8_t handle_a4_command(const real_board_legacy_frame_t *frame,
                                 cJSON **response_data,
                                 int *code,
                                 const char **message,
                                 uint8_t *reboot_requested)
{
    uint8_t response_payload[REAL_BOARD_RAW_FRAME_MAX];
    uint16_t response_payload_len = 0U;

    memset(response_payload, 0, sizeof(response_payload));
    if (!real_board_ota_handle_legacy_payload(frame->cmd_type,
                                              frame->payload,
                                              frame->payload_len,
                                              response_payload,
                                              sizeof(response_payload),
                                              &response_payload_len,
                                              code,
                                              message,
                                              reboot_requested))
    {
        return create_legacy_received_data(frame, response_data, code, message);
    }

    if (response_payload_len == 0U)
    {
        response_payload[0] = (*code == 0) ? 0x01U : 0x00U;
        response_payload_len = 1U;
    }

    *response_data = create_legacy_response_data(frame, response_payload, response_payload_len, "mainboard_upgrade");
    return 1;
}

static uint8_t handle_a5_command(const real_board_legacy_frame_t *frame,
                                 cJSON **response_data,
                                 int *code,
                                 const char **message,
                                 uint8_t *reboot_requested)
{
    uint8_t response_payload[4] = {0};
    uint8_t hardware_accepted;
    int slot;

    if (handle_a5_nested_payload_frame(frame, response_data, code, message, reboot_requested))
        return 1;

    real_board_state.last_peripheral_cmd = frame->cmd_type;
    real_board_state.last_peripheral_payload_len = frame->payload_len;

    if (find_a5_slot(frame->cmd_type, 1) >= 0)
    {
        response_payload[0] = 0x07U;
        *code = 503;
        *message = "outside-device channel busy";
        *response_data = create_legacy_response_data(frame, response_payload, 1U, "outside_device_passthrough");
        if (*response_data != NULL)
            cJSON_AddStringToObject(*response_data, "state", "busy");
        return 1;
    }

    if (find_a5_slot(frame->cmd_type, 0) < 0)
    {
        response_payload[0] = 0x07U;
        *code = 503;
        *message = "outside-device channel busy";
        *response_data = create_legacy_response_data(frame, response_payload, 1U, "outside_device_passthrough");
        if (*response_data != NULL)
            cJSON_AddStringToObject(*response_data, "state", "busy");
        return 1;
    }

    hardware_accepted = real_board_hardware_passthrough_a5(frame->cmd_type, frame->payload, frame->payload_len);
    if (!hardware_accepted)
    {
        response_payload[0] = 0x07U;
        *code = 503;
        *message = "outside-device channel busy";
        *response_data = create_legacy_response_data(frame, response_payload, 1U, "outside_device_passthrough");
        if (*response_data != NULL)
            cJSON_AddStringToObject(*response_data, "state", "busy");
        return 1;
    }

    slot = queue_a5_request(frame);
    response_payload[0] = 0x00U;
    *message = "legacy outside-device payload accepted and pending";
    *response_data = create_legacy_response_data(frame, response_payload, 1U, "outside_device_passthrough");
    if (*response_data != NULL)
    {
        cJSON_AddStringToObject(*response_data, "state", "pending");
        cJSON_AddNumberToObject(*response_data, "pending_slot", slot);
        cJSON_AddNumberToObject(*response_data, "timeout_ticks", REAL_BOARD_A5_TIMEOUT_TICKS);
    }
    return 1;
}

void real_board_business_init(void)
{
    memset(&real_board_state, 0, sizeof(real_board_state));
    memset(a5_pending, 0, sizeof(a5_pending));
    clear_timer_list(real_board_state.timer_ctl_1);
    clear_timer_list(real_board_state.timer_ctl_2);
    real_board_state.address = 1U;
    real_board_state.keepalive_s = 300U;
    real_board_state.rs485_read_period_s = 30U;
    real_board_state.device_type = 0x4005U;
    real_board_state.test_load_power_w = 0U;
    real_board_state.reg_device_flag = 0U;
}

void real_board_business_tick(void)
{
    real_board_state.uptime_ticks++;

    for (uint8_t i = 0; i < REAL_BOARD_A5_PENDING_MAX; i++)
    {
        if (a5_pending[i].used &&
            a5_pending[i].state == REAL_BOARD_A5_SLOT_PENDING &&
            (real_board_state.uptime_ticks - a5_pending[i].request_tick) >= REAL_BOARD_A5_TIMEOUT_TICKS)
        {
            a5_pending[i].state = REAL_BOARD_A5_SLOT_TIMEOUT;
        }
    }

    if ((real_board_state.relay1_on || real_board_state.relay2_on) &&
        ((real_board_state.lamp1_brightness > 0U) || (real_board_state.lamp2_brightness > 0U)))
    {
        real_board_state.open_lamp_seconds++;
        real_board_state.bright_time_seconds++;
        real_board_state.bright_time_total_seconds++;
    }
}

void real_board_business_update_meter_hlw8112(const real_board_meter_hlw8112_t *meter)
{
    if (meter == NULL)
    {
        real_board_state.meter_hlw8112_valid = 0U;
        return;
    }

    real_board_state.meter_hlw8112_valid = meter->valid ? 1U : 0U;
    real_board_state.voltage_mv = meter->voltage_mv;
    real_board_state.current_ma = meter->current_ma;
    real_board_state.active_power_mw = meter->active_power_mw;
    real_board_state.reactive_power_mvar = meter->reactive_power_mvar;
    real_board_state.power_factor_x1000 = meter->power_factor_x1000;
    real_board_state.energy_one_wh = meter->energy_one_wh;
    real_board_state.energy_total_wh = meter->energy_total_wh;
    real_board_state.pulse_count = meter->pulse_count;
}

void real_board_business_update_environment(const real_board_environment_t *environment)
{
    if (environment == NULL)
    {
        real_board_state.environment_valid = 0U;
        return;
    }

    real_board_state.environment_valid = environment->valid ? 1U : 0U;
    real_board_state.environment_temperature_c_x10 = environment->temperature_c_x10;
    real_board_state.environment_humidity_rh_x10 = environment->humidity_rh_x10;
    real_board_state.environment_pm25_ugm3 = environment->pm25_ugm3;
    real_board_state.environment_co2_ppm = environment->co2_ppm;
    real_board_state.environment_illuminance_lux = environment->illuminance_lux;
}

void real_board_business_update_rs485(const real_board_rs485_t *rs485)
{
    if (rs485 == NULL)
    {
        real_board_state.rs485_valid = 0U;
        real_board_state.rs485_online = 0U;
        return;
    }

    real_board_state.rs485_valid = rs485->valid ? 1U : 0U;
    real_board_state.rs485_online = rs485->online ? 1U : 0U;
    real_board_state.rs485_device_count = rs485->device_count;
    real_board_state.rs485_last_response_ms = rs485->last_response_ms;
    real_board_state.rs485_tx_count = rs485->tx_count;
    real_board_state.rs485_rx_count = rs485->rx_count;
    real_board_state.rs485_error_count = rs485->error_count;
}

uint8_t real_board_business_handle_peripheral_response(uint8_t cmd_type, const uint8_t *payload, uint16_t payload_len)
{
    int slot = find_a5_slot(cmd_type, 1);
    uint16_t copy_len = payload_len;

    if (slot < 0)
        return 0;

    if (copy_len > REAL_BOARD_A5_RESPONSE_MAX)
        copy_len = REAL_BOARD_A5_RESPONSE_MAX;

    a5_pending[slot].state = REAL_BOARD_A5_SLOT_RESPONSE;
    a5_pending[slot].response_len = copy_len;
    if (copy_len > 0U && payload != NULL)
        memcpy(a5_pending[slot].response, payload, copy_len);
    return 1;
}

void real_board_business_append_telemetry(cJSON *root)
{
    cJSON *meter;
    cJSON *environment;
    cJSON *rs485;
    cJSON *lighting;
    cJSON *device;
    cJSON *peripherals;
    cJSON *factory;
    cJSON *legacy;

    cJSON_AddStringToObject(root, "schema", "emqx-gateway.realboard.telemetry.v1");
    cJSON_AddStringToObject(root, "device_id", system_config->device_id);
    cJSON_AddStringToObject(root, "firmware_version", REAL_BOARD_FW_VERSION);
    cJSON_AddStringToObject(root, "business_mode", "real_board");

    meter = cJSON_AddObjectToObject(root, "meter_hlw8112");
    cJSON_AddBoolToObject(meter, "valid", real_board_state.meter_hlw8112_valid);
    cJSON_AddStringToObject(meter, "source", "real_driver");
    cJSON_AddNumberToObject(meter, "voltage_mv", real_board_state.voltage_mv);
    cJSON_AddNumberToObject(meter, "current_ma", real_board_state.current_ma);
    cJSON_AddNumberToObject(meter, "active_power_mw", real_board_state.active_power_mw);
    cJSON_AddNumberToObject(meter, "reactive_power_mvar", real_board_state.reactive_power_mvar);
    cJSON_AddNumberToObject(meter, "power_factor_x1000", real_board_state.power_factor_x1000);
    cJSON_AddNumberToObject(meter, "energy_one_wh", real_board_state.energy_one_wh);
    cJSON_AddNumberToObject(meter, "energy_total_wh", real_board_state.energy_total_wh);
    cJSON_AddNumberToObject(meter, "pulse_count", real_board_state.pulse_count);

    environment = cJSON_AddObjectToObject(root, "environment");
    cJSON_AddBoolToObject(environment, "valid", real_board_state.environment_valid);
    cJSON_AddStringToObject(environment, "source", "real_driver");
    cJSON_AddNumberToObject(environment, "temperature_c_x10", real_board_state.environment_temperature_c_x10);
    cJSON_AddNumberToObject(environment, "humidity_rh_x10", real_board_state.environment_humidity_rh_x10);
    cJSON_AddNumberToObject(environment, "pm25_ugm3", real_board_state.environment_pm25_ugm3);
    cJSON_AddNumberToObject(environment, "co2_ppm", real_board_state.environment_co2_ppm);
    cJSON_AddNumberToObject(environment, "illuminance_lux", real_board_state.environment_illuminance_lux);

    rs485 = cJSON_AddObjectToObject(root, "rs485");
    cJSON_AddBoolToObject(rs485, "valid", real_board_state.rs485_valid);
    cJSON_AddBoolToObject(rs485, "online", real_board_state.rs485_online);
    cJSON_AddNumberToObject(rs485, "device_count", real_board_state.rs485_device_count);
    cJSON_AddNumberToObject(rs485, "read_period_s", real_board_state.rs485_read_period_s);
    cJSON_AddNumberToObject(rs485, "last_response_ms", real_board_state.rs485_last_response_ms);
    cJSON_AddNumberToObject(rs485, "tx_count", real_board_state.rs485_tx_count);
    cJSON_AddNumberToObject(rs485, "rx_count", real_board_state.rs485_rx_count);
    cJSON_AddNumberToObject(rs485, "error_count", real_board_state.rs485_error_count);

    lighting = cJSON_AddObjectToObject(root, "lighting");
    cJSON_AddNumberToObject(lighting, "lamp1_brightness", real_board_state.lamp1_brightness);
    cJSON_AddNumberToObject(lighting, "lamp2_brightness", real_board_state.lamp2_brightness);
    cJSON_AddBoolToObject(lighting, "relay1_on", real_board_state.relay1_on);
    cJSON_AddBoolToObject(lighting, "relay2_on", real_board_state.relay2_on);
    cJSON_AddNumberToObject(lighting, "open_lamp_seconds", real_board_state.open_lamp_seconds);
    cJSON_AddNumberToObject(lighting, "bright_time_seconds", real_board_state.bright_time_seconds);
    cJSON_AddNumberToObject(lighting, "bright_time_total_seconds", real_board_state.bright_time_total_seconds);

    device = cJSON_AddObjectToObject(root, "device");
    cJSON_AddNumberToObject(device, "address", real_board_state.address);
    cJSON_AddNumberToObject(device, "keepalive_s", real_board_state.keepalive_s);
    cJSON_AddNumberToObject(device, "temperature_c_x10", real_board_state.temperature_c_x10);
    cJSON_AddNumberToObject(device, "error_code", real_board_state.error_code);
    cJSON_AddBoolToObject(device, "error_renew", real_board_state.error_renew);
    cJSON_AddNumberToObject(device, "uptime_ticks", real_board_state.uptime_ticks);

    peripherals = cJSON_AddArrayToObject(root, "peripherals");
    append_peripheral(peripherals, "lamp", 0x01U);
    append_peripheral(peripherals, "environment", 0x02U);
    append_peripheral(peripherals, "charging_c2", 0x03U);
    append_peripheral(peripherals, "charging_c3", 0x04U);
    append_peripheral(peripherals, "lean", 0x05U);
    append_peripheral(peripherals, "lora", 0x06U);
    append_peripheral(peripherals, "gps", 0x07U);

    factory = cJSON_AddObjectToObject(root, "factory_test");
    cJSON_AddNumberToObject(factory, "test_load_power_w", real_board_state.test_load_power_w);
    cJSON_AddNumberToObject(factory, "device_type", real_board_state.device_type);
    cJSON_AddNumberToObject(factory, "reg_device_flag", real_board_state.reg_device_flag);
    cJSON_AddNumberToObject(factory, "test_count", real_board_state.test_count);
    cJSON_AddNumberToObject(factory, "test_results", real_board_state.test_results);
    cJSON_AddBoolToObject(factory, "test_enable", real_board_state.test_enable);

    legacy = cJSON_AddObjectToObject(root, "legacy_protocol");
    cJSON_AddBoolToObject(legacy, "downlink_supported", REAL_BOARD_ENABLE_LEGACY_FRAME ? 1 : 0);
    cJSON_AddStringToObject(legacy, "product_key", real_board_state.legacy_product_key);
    cJSON_AddStringToObject(legacy, "device_name", real_board_state.legacy_device_name);
    cJSON_AddNumberToObject(legacy, "last_peripheral_cmd", real_board_state.last_peripheral_cmd);
    append_a5_pending(legacy);
}

void real_board_business_append_status(cJSON *object)
{
    cJSON_AddStringToObject(object, "business_mode", "real_board");
    cJSON_AddStringToObject(object, "firmware_version", REAL_BOARD_FW_VERSION);
    cJSON_AddBoolToObject(object, "legacy_downlink_supported", REAL_BOARD_ENABLE_LEGACY_FRAME ? 1 : 0);
    cJSON_AddNumberToObject(object, "address", real_board_state.address);
    cJSON_AddNumberToObject(object, "keepalive_s", real_board_state.keepalive_s);
    cJSON_AddNumberToObject(object, "error_code", real_board_state.error_code);
    cJSON_AddNumberToObject(object, "last_peripheral_cmd", real_board_state.last_peripheral_cmd);
    cJSON_AddBoolToObject(object, "meter_hlw8112_valid", real_board_state.meter_hlw8112_valid);
    cJSON_AddNumberToObject(object, "meter_voltage_mv", real_board_state.voltage_mv);
    cJSON_AddBoolToObject(object, "environment_valid", real_board_state.environment_valid);
    cJSON_AddNumberToObject(object, "environment_temperature_c_x10", real_board_state.environment_temperature_c_x10);
    cJSON_AddBoolToObject(object, "rs485_valid", real_board_state.rs485_valid);
    cJSON_AddBoolToObject(object, "rs485_online", real_board_state.rs485_online);
    append_a5_pending(object);
}

uint8_t real_board_business_handle_json_command(cJSON *root,
                                                const char *cmd,
                                                cJSON **response_data,
                                                int *code,
                                                const char **message)
{
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    cJSON *item;

    *response_data = NULL;
    *code = 0;
    *message = "ok";

    if ((strcmp(cmd, "real_set") == 0) || (strcmp(cmd, "sim_set") == 0))
    {
        if (!cJSON_IsObject(params))
        {
            *code = 422;
            *message = "params must be an object";
            return 1;
        }

        item = cJSON_GetObjectItemCaseSensitive(params, "relay1_on");
        if (item != NULL)
            real_board_state.relay1_on = cJSON_IsTrue(item) ? 1U : 0U;
        item = cJSON_GetObjectItemCaseSensitive(params, "relay2_on");
        if (item != NULL)
            real_board_state.relay2_on = cJSON_IsTrue(item) ? 1U : 0U;

        if (!json_optional_u8(params, "lamp1_brightness", &real_board_state.lamp1_brightness) ||
            !json_optional_u8(params, "lamp2_brightness", &real_board_state.lamp2_brightness))
        {
            *code = 422;
            *message = "real_set parameter is out of range";
            return 1;
        }

        *response_data = cJSON_CreateObject();
        if (*response_data != NULL)
        {
            cJSON *ignored = cJSON_AddArrayToObject(*response_data, "ignored_real_only_fields");
            cJSON_AddStringToObject(*response_data, "mode", "real_board");
            cJSON_AddBoolToObject(*response_data, "telemetry_fields_written", 0);
            cJSON_AddBoolToObject(*response_data, "compat_alias", (strcmp(cmd, "sim_set") == 0));
            if (ignored != NULL)
            {
                if (cJSON_GetObjectItemCaseSensitive(params, "voltage_mv") != NULL)
                    cJSON_AddItemToArray(ignored, cJSON_CreateString("voltage_mv"));
                if (cJSON_GetObjectItemCaseSensitive(params, "current_ma") != NULL)
                    cJSON_AddItemToArray(ignored, cJSON_CreateString("current_ma"));
                if (cJSON_GetObjectItemCaseSensitive(params, "temperature_c_x10") != NULL)
                    cJSON_AddItemToArray(ignored, cJSON_CreateString("temperature_c_x10"));
                if (cJSON_GetObjectItemCaseSensitive(params, "rs485_online") != NULL)
                    cJSON_AddItemToArray(ignored, cJSON_CreateString("rs485_online"));
            }
            real_board_business_append_status(*response_data);
        }
        *message = (strcmp(cmd, "sim_set") == 0) ? "real board command accepted; telemetry fields ignored" : "real board state updated";
        return 1;
    }

    if ((strcmp(cmd, "real_event") == 0) || (strcmp(cmd, "sim_event") == 0))
    {
        const char *event_name = "manual_test";
        const char *level = "info";

        if (cJSON_IsObject(params))
        {
            item = cJSON_GetObjectItemCaseSensitive(params, "event");
            if (cJSON_IsString(item) && item->valuestring != NULL)
                event_name = item->valuestring;
            item = cJSON_GetObjectItemCaseSensitive(params, "level");
            if (cJSON_IsString(item) && item->valuestring != NULL)
                level = item->valuestring;
        }

        queue_event(event_name, level);
        *response_data = cJSON_CreateObject();
        if (*response_data != NULL)
        {
            cJSON_AddStringToObject(*response_data, "queued_event", event_name);
            cJSON_AddStringToObject(*response_data, "level", level);
            cJSON_AddStringToObject(*response_data, "mode", "real_board");
        }
        *message = (strcmp(cmd, "sim_event") == 0) ? "real board event queued via compatibility command" : "real board event queued";
        return 1;
    }

    return 0;
}

uint8_t real_board_business_publish_event(cJSON *root)
{
    if (real_board_state.last_event_tick == real_board_state.uptime_ticks)
        return 0;

    if (real_board_state.pending_event[0] == '\0')
        return 0;

    append_event_payload(root, real_board_state.pending_event, real_board_state.pending_event_level);
    real_board_state.pending_event[0] = '\0';
    real_board_state.pending_event_level[0] = '\0';
    real_board_state.last_event_tick = real_board_state.uptime_ticks;
    return 1;
}

uint8_t real_board_business_handle_legacy_command(cJSON *root,
                                                  const char *cmd,
                                                  cJSON **response_data,
                                                  int *code,
                                                  const char **message,
                                                  uint8_t *reboot_requested)
{
#if REAL_BOARD_ENABLE_LEGACY_FRAME
    const char *frame_text;
    uint8_t raw_frame[REAL_BOARD_RAW_FRAME_MAX];
    size_t raw_frame_len = 0;
    real_board_legacy_frame_t frame;
    char error[96];

    *response_data = NULL;
    *code = 0;
    *message = "ok";
    *reboot_requested = 0U;

    frame_text = find_legacy_frame_text(root);
    if ((cmd != NULL) && (strcmp(cmd, "legacy_frame") != 0) && (frame_text == NULL))
        return 0;
    if (frame_text == NULL)
    {
        *code = 400;
        *message = "legacy frame is required";
        return 1;
    }
    if (!decode_legacy_frame(frame_text, raw_frame, sizeof(raw_frame), &raw_frame_len))
    {
        *code = 400;
        *message = "legacy frame must be valid Base64 or hex";
        return 1;
    }
    if (!parse_legacy_frame(raw_frame, raw_frame_len, &frame, error, sizeof(error)))
    {
        cJSON *data = cJSON_CreateObject();
        if (data != NULL)
        {
            cJSON_AddStringToObject(data, "legacy_protocol", "parse_error");
            cJSON_AddStringToObject(data, "error", error);
        }
        *response_data = data;
        *code = 422;
        *message = "legacy frame parse failed";
        return 1;
    }

    switch (frame.frm_type)
    {
    case REAL_BOARD_FRM_TYPE_MAINBOARD_PARA_SET:
        return handle_a1_command(&frame, response_data, code, message, reboot_requested);
    case REAL_BOARD_FRM_TYPE_MAINBOARD_CONTROL:
        return handle_a2_command(&frame, response_data, code, message, reboot_requested);
    case REAL_BOARD_FRM_TYPE_OUTSIDE_DEVICE:
        return handle_a5_command(&frame, response_data, code, message, reboot_requested);
    case REAL_BOARD_FRM_TYPE_MAINBOARD_UPGRADE:
        return handle_a4_command(&frame, response_data, code, message, reboot_requested);
    case REAL_BOARD_FRM_TYPE_MAINBOARD_DEBUG_MSG:
        return create_legacy_received_data(&frame, response_data, code, message);
    default:
        return create_legacy_received_data(&frame, response_data, code, message);
    }
#else
    (void)root;
    (void)cmd;
    *response_data = NULL;
    *code = 404;
    *message = "legacy frame parser is disabled";
    *reboot_requested = 0U;
    return 0;
#endif
}
