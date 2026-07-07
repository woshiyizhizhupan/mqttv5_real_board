#include "real_board_business.h"

#include "config.h"
#include "mbedtls/base64.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#define REAL_BOARD_RAW_FRAME_MAX 1500U
#define REAL_BOARD_BASE64_MAX 1400U
#define REAL_BOARD_LEGACY_TEXT_MAX 96U

typedef struct
{
    uint8_t frm_type;
    uint8_t cmd_type;
    uint16_t payload_len;
    const uint8_t *payload;
} real_board_legacy_frame_t;

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

    uint8_t lamp1_brightness;
    uint8_t lamp2_brightness;
    uint8_t relay1_on;
    uint8_t relay2_on;
    uint32_t open_lamp_seconds;
    uint32_t bright_time_seconds;
    uint32_t bright_time_total_seconds;

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
} real_board_state_t;

static real_board_state_t real_board_state;

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

static uint8_t decode_legacy_frame(const char *text, uint8_t *out, size_t out_size, size_t *out_len)
{
    size_t olen = 0;

    if (text == NULL)
        return 0;

    if (mbedtls_base64_decode(out, out_size, &olen, (const unsigned char *)text, strlen(text)) != 0)
        return 0;

    *out_len = olen;
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
    size_t response_len;
    cJSON *data = cJSON_CreateObject();

    if (data == NULL)
        return NULL;

    response_len = build_legacy_response_frame((uint8_t)(0x80U | frame->cmd_type),
                                               response_payload,
                                               response_payload_len,
                                               response_frame,
                                               sizeof(response_frame));
    cJSON_AddBoolToObject(data, "legacy_protocol", 1);
    cJSON_AddNumberToObject(data, "frame_type", frame->frm_type);
    cJSON_AddNumberToObject(data, "cmd_type", frame->cmd_type);
    cJSON_AddStringToObject(data, "action", action);
    if ((response_len > 0U) && encode_legacy_frame(response_frame, response_len, response_base64, sizeof(response_base64)))
        cJSON_AddStringToObject(data, "frame", response_base64);
    else
        cJSON_AddStringToObject(data, "frame", "");

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

static void append_peripheral(cJSON *array, const char *name, uint8_t legacy_cmd)
{
    cJSON *item = cJSON_CreateObject();
    if (item == NULL)
        return;

    cJSON_AddStringToObject(item, "name", name);
    cJSON_AddNumberToObject(item, "legacy_cmd", legacy_cmd);
    cJSON_AddBoolToObject(item, "configured", (real_board_state.peripheral_type_code != 0U));
    cJSON_AddBoolToObject(item, "last_downlink", real_board_state.last_peripheral_cmd == legacy_cmd);
    cJSON_AddItemToArray(array, item);
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

static uint8_t handle_a5_command(const real_board_legacy_frame_t *frame,
                                 cJSON **response_data,
                                 int *code,
                                 const char **message)
{
    uint8_t response_payload[4] = {0};
    (void)code;

    real_board_state.last_peripheral_cmd = frame->cmd_type;
    real_board_state.last_peripheral_payload_len = frame->payload_len;
    response_payload[0] = 0x00U;
    *message = "legacy outside-device payload accepted";
    *response_data = create_legacy_response_data(frame, response_payload, 1U, "outside_device_passthrough");
    return 1;
}

void real_board_business_init(void)
{
    memset(&real_board_state, 0, sizeof(real_board_state));
    real_board_state.address = 1U;
    real_board_state.keepalive_s = 300U;
    real_board_state.temperature_c_x10 = 250;
    real_board_state.power_factor_x1000 = 1000U;
    real_board_state.rs485_read_period_s = 30U;
    real_board_state.peripheral_type_code = 0x6801U;
    real_board_state.device_type = 0x4005U;
    real_board_state.test_load_power_w = 0U;
    real_board_state.reg_device_flag = 0U;
}

void real_board_business_tick(void)
{
    real_board_state.uptime_ticks++;

    if ((real_board_state.relay1_on || real_board_state.relay2_on) && (real_board_state.lamp1_brightness > 0U))
    {
        real_board_state.open_lamp_seconds++;
        real_board_state.bright_time_seconds++;
        real_board_state.bright_time_total_seconds++;
    }
}

void real_board_business_append_telemetry(cJSON *root)
{
    cJSON *electrical;
    cJSON *lighting;
    cJSON *device;
    cJSON *peripherals;
    cJSON *factory;
    cJSON *legacy;

    cJSON_AddStringToObject(root, "schema", "emqx-gateway.realboard.telemetry.v1");
    cJSON_AddStringToObject(root, "device_id", system_config->device_id);
    cJSON_AddStringToObject(root, "business_mode", "real_board");

    electrical = cJSON_AddObjectToObject(root, "electrical");
    cJSON_AddNumberToObject(electrical, "voltage_mv", real_board_state.voltage_mv);
    cJSON_AddNumberToObject(electrical, "current_ma", real_board_state.current_ma);
    cJSON_AddNumberToObject(electrical, "active_power_mw", real_board_state.active_power_mw);
    cJSON_AddNumberToObject(electrical, "reactive_power_mvar", real_board_state.reactive_power_mvar);
    cJSON_AddNumberToObject(electrical, "power_factor_x1000", real_board_state.power_factor_x1000);
    cJSON_AddNumberToObject(electrical, "energy_one_wh", real_board_state.energy_one_wh);
    cJSON_AddNumberToObject(electrical, "energy_total_wh", real_board_state.energy_total_wh);
    cJSON_AddNumberToObject(electrical, "pulse_count", real_board_state.pulse_count);

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
    cJSON_AddBoolToObject(legacy, "downlink_supported", 1);
    cJSON_AddStringToObject(legacy, "product_key", real_board_state.legacy_product_key);
    cJSON_AddStringToObject(legacy, "device_name", real_board_state.legacy_device_name);
    cJSON_AddNumberToObject(legacy, "last_peripheral_cmd", real_board_state.last_peripheral_cmd);
}

void real_board_business_append_status(cJSON *object)
{
    cJSON_AddStringToObject(object, "business_mode", "real_board");
    cJSON_AddBoolToObject(object, "legacy_downlink_supported", 1);
    cJSON_AddNumberToObject(object, "address", real_board_state.address);
    cJSON_AddNumberToObject(object, "keepalive_s", real_board_state.keepalive_s);
    cJSON_AddNumberToObject(object, "error_code", real_board_state.error_code);
    cJSON_AddNumberToObject(object, "last_peripheral_cmd", real_board_state.last_peripheral_cmd);
}

uint8_t real_board_business_handle_legacy_command(cJSON *root,
                                                  const char *cmd,
                                                  cJSON **response_data,
                                                  int *code,
                                                  const char **message,
                                                  uint8_t *reboot_requested)
{
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
        *message = "legacy frame must be valid Base64";
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
        return handle_a5_command(&frame, response_data, code, message);
    case REAL_BOARD_FRM_TYPE_MAINBOARD_DEBUG_MSG:
    case REAL_BOARD_FRM_TYPE_MAINBOARD_UPGRADE:
        return create_legacy_received_data(&frame, response_data, code, message);
    default:
        return create_legacy_received_data(&frame, response_data, code, message);
    }
}
