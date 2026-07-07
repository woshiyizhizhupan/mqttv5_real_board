#include "sim_board_ota.h"

#include "driver.h"
#include "mbedtls/base64.h"
#include "stdio.h"
#include "string.h"

#define SIM_BOARD_OTA_BASE64_MAX 1500U
#define SIM_BOARD_LEGACY_FRAME_PREAMBLE 0xFEU
#define SIM_BOARD_LEGACY_FRAME_TYPE_UPGRADE 0xA4U
#define SIM_BOARD_LEGACY_CMD_B0_START 0xB0U
#define SIM_BOARD_LEGACY_CMD_B1_FILE_MSG 0xB1U
#define SIM_BOARD_LEGACY_CMD_B2_FILE_CHUNK 0xB2U
#define SIM_BOARD_LEGACY_CMD_B3_CHECK_LOST 0xB3U
#define SIM_BOARD_LEGACY_CMD_B4_END 0xB4U
#define SIM_BOARD_LEGACY_CMD_B5_RESULT 0xB5U
#define SIM_BOARD_LEGACY_CMD_B6_BREAK 0xB6U
#define SIM_BOARD_LEGACY_RESPONSE_MAX 1500U

typedef struct
{
    uint8_t active;
    uint8_t complete;
    uint8_t break_upgrade_flag;
    char session_id[40];
    uint32_t file_len;
    uint32_t file_crc32;
    uint16_t chunk_size;
    uint16_t chunk_count;
    uint32_t received_bytes;
    uint16_t upgrade_state;
    uint8_t pack_flag[SIM_BOARD_OTA_PACK_FLAG_BYTES];
    uint8_t sector_erased[SIM_BOARD_APP2_SECTOR_COUNT];
    char last_error[80];
} sim_board_ota_state_t;

static sim_board_ota_state_t ota_state;

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, uint32_t size)
{
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
    return crc;
}

static uint32_t crc32_bytes(const uint8_t *buf, uint32_t size)
{
    return crc32_update(0xFFFFFFFFUL, buf, size) ^ 0xFFFFFFFFUL;
}

static uint16_t read_u16_be(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static uint32_t read_u32_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
}

static void write_u16_be(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)((value >> 8) & 0xFFU);
    buf[1] = (uint8_t)(value & 0xFFU);
}

static void write_u32_be(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)((value >> 24) & 0xFFU);
    buf[1] = (uint8_t)((value >> 16) & 0xFFU);
    buf[2] = (uint8_t)((value >> 8) & 0xFFU);
    buf[3] = (uint8_t)(value & 0xFFU);
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

static uint32_t crc32_flash(uint32_t addr, uint32_t size)
{
    uint8_t buf[128];
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t remaining = size;

    while (remaining > 0U)
    {
        uint16_t read_len = remaining > sizeof(buf) ? sizeof(buf) : (uint16_t)remaining;
        flash_read(addr, buf, read_len);
        crc = crc32_update(crc, buf, read_len);
        addr += read_len;
        remaining -= read_len;
    }

    return crc ^ 0xFFFFFFFFUL;
}

static void set_last_error(const char *error)
{
    snprintf(ota_state.last_error, sizeof(ota_state.last_error), "%s", error);
}

static void ota_pack_flag_set(uint16_t index)
{
    if (index >= SIM_BOARD_OTA_MAX_CHUNKS)
        return;
    ota_state.pack_flag[index / 8U] |= (uint8_t)(1U << (index % 8U));
}

static uint8_t ota_pack_flag_is_set(uint16_t index)
{
    if (index >= SIM_BOARD_OTA_MAX_CHUNKS)
        return 0U;
    return (ota_state.pack_flag[index / 8U] & (uint8_t)(1U << (index % 8U))) ? 1U : 0U;
}

static uint8_t json_get_string(cJSON *params, const char *name, const char **value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(params, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL)
        return 0;
    *value = item->valuestring;
    return 1;
}

static uint8_t json_get_u32(cJSON *params, const char *name, uint32_t *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(params, name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 || item->valuedouble > 4294967295.0)
        return 0;
    *value = (uint32_t)item->valuedouble;
    return 1;
}

static uint8_t json_get_u16(cJSON *params, const char *name, uint16_t *value)
{
    uint32_t temp;
    if (!json_get_u32(params, name, &temp) || temp > 65535UL)
        return 0;
    *value = (uint16_t)temp;
    return 1;
}

static void append_status(cJSON *object)
{
    cJSON_AddBoolToObject(object, "active", ota_state.active);
    cJSON_AddBoolToObject(object, "complete", ota_state.complete);
    cJSON_AddStringToObject(object, "session_id", ota_state.session_id);
    cJSON_AddNumberToObject(object, "app1_start", SIM_BOARD_APP1_START_ADDR);
    cJSON_AddNumberToObject(object, "app2_start", SIM_BOARD_APP2_START_ADDR);
    cJSON_AddNumberToObject(object, "app2_end", SIM_BOARD_APP2_END_ADDR);
    cJSON_AddNumberToObject(object, "upgrade_state_addr", SIM_BOARD_UPGRADE_STATE_ADDR);
    cJSON_AddNumberToObject(object, "file_len", ota_state.file_len);
    cJSON_AddNumberToObject(object, "file_crc32", ota_state.file_crc32);
    cJSON_AddNumberToObject(object, "chunk_size", ota_state.chunk_size);
    cJSON_AddNumberToObject(object, "chunk_count", ota_state.chunk_count);
    cJSON_AddNumberToObject(object, "received_bytes", ota_state.received_bytes);
    cJSON_AddNumberToObject(object, "upgrade_state", ota_state.upgrade_state);
    cJSON_AddBoolToObject(object, "break_upgrade", ota_state.break_upgrade_flag);
    cJSON_AddStringToObject(object, "last_error", ota_state.last_error);
}

static void append_transfer_ack(cJSON *object, const char *phase, uint32_t index)
{
    cJSON_AddStringToObject(object, "phase", phase);
    cJSON_AddBoolToObject(object, "active", ota_state.active);
    cJSON_AddBoolToObject(object, "complete", ota_state.complete);
    cJSON_AddStringToObject(object, "session_id", ota_state.session_id);
    cJSON_AddNumberToObject(object, "file_len", ota_state.file_len);
    cJSON_AddNumberToObject(object, "chunk_size", ota_state.chunk_size);
    cJSON_AddNumberToObject(object, "chunk_count", ota_state.chunk_count);
    cJSON_AddNumberToObject(object, "received_bytes", ota_state.received_bytes);
    if (index != 0xFFFFFFFFUL)
        cJSON_AddNumberToObject(object, "index", index);
}

static uint8_t session_matches(const char *session_id)
{
    return (ota_state.active || ota_state.complete) && (strcmp(session_id, ota_state.session_id) == 0);
}

static uint8_t ensure_sector_erased(uint32_t addr, uint32_t end_addr)
{
    uint32_t first_sector;
    uint32_t last_sector;

    if ((addr < SIM_BOARD_APP2_START_ADDR) || (end_addr > SIM_BOARD_APP2_END_ADDR) || (addr >= end_addr))
        return 0;

    first_sector = (addr - SIM_BOARD_APP2_START_ADDR) / SIM_BOARD_FLASH_PAGE_SIZE;
    last_sector = (end_addr - SIM_BOARD_APP2_START_ADDR - 1U) / SIM_BOARD_FLASH_PAGE_SIZE;
    if (last_sector >= SIM_BOARD_APP2_SECTOR_COUNT)
        return 0;

    for (uint32_t sector = first_sector; sector <= last_sector; sector++)
    {
        if (ota_state.sector_erased[sector] == 0U)
        {
            flash_erase_sector(SIM_BOARD_APP2_START_ADDR + (sector * SIM_BOARD_FLASH_PAGE_SIZE));
            ota_state.sector_erased[sector] = 1U;
        }
    }
    return 1;
}

static uint8_t write_chunk(uint32_t offset, const uint8_t *data, uint32_t data_len)
{
    uint8_t write_buf[SIM_BOARD_OTA_MAX_CHUNK_SIZE + 4U];
    uint8_t verify_buf[SIM_BOARD_OTA_MAX_CHUNK_SIZE + 4U];
    uint32_t addr = SIM_BOARD_APP2_START_ADDR + offset;
    uint32_t write_len = (data_len + 3U) & ~3U;
    uint32_t end_addr = addr + write_len;

    if ((addr < SIM_BOARD_APP2_START_ADDR) || (end_addr > SIM_BOARD_APP2_END_ADDR))
    {
        set_last_error("OTA chunk address is outside APP2");
        return 0;
    }
    if (write_len > sizeof(write_buf))
    {
        set_last_error("OTA chunk is too large");
        return 0;
    }
    if (!ensure_sector_erased(addr, end_addr))
    {
        set_last_error("OTA sector erase failed");
        return 0;
    }

    memset(write_buf, 0xFFU, write_len);
    memcpy(write_buf, data, data_len);
    flash_write(addr, write_buf, (uint16_t)write_len);
    flash_read(addr, verify_buf, (uint16_t)write_len);
    if (memcmp(write_buf, verify_buf, write_len) != 0)
    {
        set_last_error("OTA flash verify failed");
        return 0;
    }
    return 1;
}

static uint8_t all_chunks_received(void)
{
    if (ota_state.chunk_count == 0U || ota_state.chunk_count > SIM_BOARD_OTA_MAX_CHUNKS)
        return 0;
    for (uint16_t i = 0; i < ota_state.chunk_count; i++)
    {
        if (!ota_pack_flag_is_set(i))
            return 0;
    }
    return 1;
}

static void write_upgrade_handle(void)
{
    sim_board_ota_legacy_handle_t handle;
    uint8_t write_buf[sizeof(sim_board_ota_legacy_handle_t) + 4U];
    uint32_t write_len = (sizeof(sim_board_ota_legacy_handle_t) + 3U) & ~3U;

    memset(&handle, 0, sizeof(handle));
    handle.file_msg.file_len = ota_state.file_len;
    handle.file_msg.frm_total = ota_state.chunk_count;
    handle.file_msg.frm_data_len = ota_state.chunk_size;
    handle.file_msg.file_crc = ota_state.file_crc32;
    handle.upgrade_state = ota_state.upgrade_state;
    handle.break_upgrade_flag = ota_state.break_upgrade_flag;
    memcpy(handle.pack_flag, ota_state.pack_flag, sizeof(handle.pack_flag));

    memset(write_buf, 0xFFU, write_len);
    memcpy(write_buf, &handle, sizeof(handle));
    flash_erase_sector(SIM_BOARD_UPGRADE_STATE_ADDR);
    flash_write(SIM_BOARD_UPGRADE_STATE_ADDR, write_buf, (uint16_t)write_len);
}

static uint8_t handle_begin(cJSON *params, cJSON **response_data, int *code, const char **message)
{
    const char *session_id;
    uint32_t file_len;
    uint32_t file_crc32;
    uint16_t chunk_size;
    uint16_t chunk_count;

    if (!cJSON_IsObject(params) ||
        !json_get_string(params, "session_id", &session_id) ||
        !json_get_u32(params, "file_len", &file_len) ||
        !json_get_u32(params, "file_crc32", &file_crc32) ||
        !json_get_u16(params, "chunk_size", &chunk_size) ||
        !json_get_u16(params, "chunk_count", &chunk_count))
    {
        *code = 422;
        *message = "ota_begin params are invalid";
        return 1;
    }

    if (file_len == 0U || file_len > (SIM_BOARD_APP2_END_ADDR - SIM_BOARD_APP2_START_ADDR))
    {
        *code = 422;
        *message = "OTA image length exceeds APP2";
        return 1;
    }
    if (chunk_size == 0U || chunk_size > SIM_BOARD_OTA_MAX_CHUNK_SIZE || chunk_count == 0U || chunk_count > SIM_BOARD_OTA_MAX_CHUNKS)
    {
        *code = 422;
        *message = "OTA chunk settings are invalid";
        return 1;
    }
    if (((uint32_t)(chunk_count - 1U) * chunk_size) >= file_len)
    {
        *code = 422;
        *message = "OTA chunk count does not match image length";
        return 1;
    }
    if (((uint32_t)chunk_count * chunk_size) < file_len)
    {
        *code = 422;
        *message = "OTA chunk count is too small";
        return 1;
    }
    memset(&ota_state, 0, sizeof(ota_state));
    ota_state.active = 1U;
    snprintf(ota_state.session_id, sizeof(ota_state.session_id), "%s", session_id);
    ota_state.file_len = file_len;
    ota_state.file_crc32 = file_crc32;
    ota_state.chunk_size = chunk_size;
    ota_state.chunk_count = chunk_count;

    *response_data = cJSON_CreateObject();
    if (*response_data != NULL)
        append_transfer_ack(*response_data, "begin", 0xFFFFFFFFUL);
    *message = "OTA session started";
    return 1;
}

static uint8_t handle_chunk(cJSON *params, cJSON **response_data, int *code, const char **message)
{
    const char *session_id;
    const char *base64_data;
    uint32_t index;
    uint32_t offset;
    uint32_t chunk_crc32;
    uint8_t decode_buf[SIM_BOARD_OTA_MAX_CHUNK_SIZE];
    size_t decoded_len = 0;

    if (!cJSON_IsObject(params) ||
        !json_get_string(params, "session_id", &session_id) ||
        !json_get_u32(params, "index", &index) ||
        !json_get_u32(params, "offset", &offset) ||
        !json_get_u32(params, "chunk_crc32", &chunk_crc32) ||
        !json_get_string(params, "data", &base64_data))
    {
        *code = 422;
        *message = "ota_chunk params are invalid";
        return 1;
    }
    if (!session_matches(session_id))
    {
        *code = 409;
        *message = "OTA session mismatch";
        return 1;
    }
    if (index >= ota_state.chunk_count || index >= SIM_BOARD_OTA_MAX_CHUNKS || offset >= ota_state.file_len)
    {
        *code = 422;
        *message = "OTA chunk index or offset is invalid";
        return 1;
    }
    if (strlen(base64_data) > SIM_BOARD_OTA_BASE64_MAX ||
        mbedtls_base64_decode(decode_buf, sizeof(decode_buf), &decoded_len, (const unsigned char *)base64_data, strlen(base64_data)) != 0)
    {
        *code = 422;
        *message = "OTA chunk data is not valid Base64";
        return 1;
    }
    if (decoded_len == 0U || decoded_len > ota_state.chunk_size || (offset + decoded_len) > ota_state.file_len)
    {
        *code = 422;
        *message = "OTA chunk length is invalid";
        return 1;
    }
    if (crc32_bytes(decode_buf, (uint32_t)decoded_len) != chunk_crc32)
    {
        *code = 422;
        *message = "OTA chunk CRC mismatch";
        return 1;
    }
    if (!write_chunk(offset, decode_buf, (uint32_t)decoded_len))
    {
        *code = 500;
        *message = ota_state.last_error;
        return 1;
    }

    if (!ota_pack_flag_is_set((uint16_t)index))
        ota_state.received_bytes += (uint32_t)decoded_len;
    ota_pack_flag_set((uint16_t)index);

    *response_data = cJSON_CreateObject();
    if (*response_data != NULL)
        append_transfer_ack(*response_data, "chunk", index);
    *message = "OTA chunk written";
    return 1;
}

static uint8_t handle_end(cJSON *params, cJSON **response_data, int *code, const char **message, uint8_t *reboot_requested)
{
    const char *session_id;
    uint32_t flash_crc;

    if (!cJSON_IsObject(params) || !json_get_string(params, "session_id", &session_id))
    {
        *code = 422;
        *message = "ota_end params are invalid";
        return 1;
    }
    if (!session_matches(session_id))
    {
        *code = 409;
        *message = "OTA session mismatch";
        return 1;
    }
    if (!all_chunks_received())
    {
        *code = 409;
        *message = "OTA chunks are incomplete";
        return 1;
    }
    flash_crc = crc32_flash(SIM_BOARD_APP2_START_ADDR, ota_state.file_len);
    if (flash_crc != ota_state.file_crc32)
    {
        set_last_error("OTA image CRC mismatch");
        *code = 422;
        *message = ota_state.last_error;
        return 1;
    }

    ota_state.upgrade_state = SIM_BOARD_UPGRADE_STATE_SUCCESS;
    write_upgrade_handle();
    ota_state.active = 0U;
    ota_state.complete = 1U;
    *reboot_requested = 1U;

    *response_data = cJSON_CreateObject();
    if (*response_data != NULL)
        append_transfer_ack(*response_data, "end", 0xFFFFFFFFUL);
    *message = "OTA image verified; reboot scheduled";
    return 1;
}

static cJSON *create_legacy_upgrade_response(uint8_t cmd_type, const uint8_t *payload, uint16_t payload_len, const char *phase)
{
    uint8_t frame[SIM_BOARD_LEGACY_RESPONSE_MAX];
    char encoded[SIM_BOARD_OTA_BASE64_MAX];
    char encoded_hex[(SIM_BOARD_LEGACY_RESPONSE_MAX * 2U) + 1U];
    size_t encoded_len = 0;
    size_t cursor = 0;
    uint32_t crc;
    cJSON *data = cJSON_CreateObject();

    if (data == NULL)
        return NULL;

    if (sizeof(frame) >= (size_t)(1U + 1U + 1U + 2U + payload_len + 4U))
    {
        frame[cursor++] = SIM_BOARD_LEGACY_FRAME_PREAMBLE;
        frame[cursor++] = SIM_BOARD_LEGACY_FRAME_TYPE_UPGRADE;
        frame[cursor++] = cmd_type;
        write_u16_be(&frame[cursor], payload_len);
        cursor += 2U;
        if (payload_len > 0U && payload != NULL)
        {
            memcpy(&frame[cursor], payload, payload_len);
            cursor += payload_len;
        }
        crc = crc32_bytes(&frame[1], (uint32_t)(cursor - 1U));
        write_u32_be(&frame[cursor], crc);
        cursor += 4U;

        if (mbedtls_base64_encode((unsigned char *)encoded, sizeof(encoded), &encoded_len, frame, cursor) == 0 &&
            encoded_len < sizeof(encoded))
        {
            encoded[encoded_len] = '\0';
            cJSON_AddStringToObject(data, "frame", encoded);
            cJSON_AddStringToObject(data, "msg", encoded);
        }
        else
        {
            cJSON_AddStringToObject(data, "frame", "");
            cJSON_AddStringToObject(data, "msg", "");
        }
        if (encode_legacy_frame_hex(frame, cursor, encoded_hex, sizeof(encoded_hex)))
        {
            cJSON_AddStringToObject(data, "frame_hex", encoded_hex);
            cJSON_AddStringToObject(data, "msg_hex", encoded_hex);
        }
        else
        {
            cJSON_AddStringToObject(data, "frame_hex", "");
            cJSON_AddStringToObject(data, "msg_hex", "");
        }
    }
    else
    {
        cJSON_AddStringToObject(data, "frame", "");
        cJSON_AddStringToObject(data, "msg", "");
        cJSON_AddStringToObject(data, "frame_hex", "");
        cJSON_AddStringToObject(data, "msg_hex", "");
    }

    cJSON_AddBoolToObject(data, "legacy_protocol", 1);
    cJSON_AddStringToObject(data, "action", "mainboard_upgrade");
    cJSON_AddStringToObject(data, "phase", phase);
    cJSON_AddNumberToObject(data, "frame_type", SIM_BOARD_LEGACY_FRAME_TYPE_UPGRADE);
    cJSON_AddNumberToObject(data, "cmd_type", cmd_type);
    cJSON_AddNumberToObject(data, "payload_len", payload_len);
    return data;
}

static uint8_t legacy_file_msg_is_valid(uint32_t file_len, uint16_t chunk_size, uint16_t chunk_count)
{
    if (file_len == 0U || file_len > (SIM_BOARD_APP2_END_ADDR - SIM_BOARD_APP2_START_ADDR))
        return 0U;
    if (chunk_size == 0U || chunk_size > SIM_BOARD_OTA_MAX_CHUNK_SIZE || chunk_count == 0U || chunk_count > SIM_BOARD_OTA_MAX_CHUNKS)
        return 0U;
    if (((uint32_t)(chunk_count - 1U) * chunk_size) >= file_len)
        return 0U;
    if (((uint32_t)chunk_count * chunk_size) < file_len)
        return 0U;
    return 1U;
}

static uint8_t legacy_start_session(uint32_t file_len, uint16_t chunk_size, uint16_t chunk_count, uint32_t file_crc32)
{
    if (!legacy_file_msg_is_valid(file_len, chunk_size, chunk_count))
        return 0U;

    memset(&ota_state, 0, sizeof(ota_state));
    ota_state.active = 1U;
    snprintf(ota_state.session_id, sizeof(ota_state.session_id), "legacy");
    ota_state.file_len = file_len;
    ota_state.file_crc32 = file_crc32;
    ota_state.chunk_size = chunk_size;
    ota_state.chunk_count = chunk_count;
    ota_state.upgrade_state = 0U;
    write_upgrade_handle();
    return 1U;
}

static uint16_t ota_first_lost_pack(void)
{
    if (ota_state.chunk_count == 0U || ota_state.chunk_count > SIM_BOARD_OTA_MAX_CHUNKS)
        return 0xFFFFU;
    for (uint16_t index = 0; index < ota_state.chunk_count; index++)
    {
        if (!ota_pack_flag_is_set(index))
            return index;
    }
    return 0xFFFFU;
}

static uint8_t handle_legacy_b0_start(cJSON **response_data, int *code, const char **message)
{
    uint8_t payload[7] = {1U, 10U, 2U, 55U, 'x', 'm', 1U};

    *code = 0;
    *message = "legacy OTA info";
    *response_data = create_legacy_upgrade_response(SIM_BOARD_LEGACY_CMD_B0_START, payload, sizeof(payload), "start");
    return 1U;
}

static uint8_t handle_legacy_b1_file_msg(const uint8_t *payload,
                                         uint16_t payload_len,
                                         cJSON **response_data,
                                         int *code,
                                         const char **message)
{
    uint8_t ack[1] = {0U};
    uint32_t file_len;
    uint16_t chunk_count;
    uint16_t chunk_size;
    uint32_t file_crc32;

    if (payload != NULL && payload_len >= 12U)
    {
        file_len = read_u32_be(&payload[0]);
        chunk_count = read_u16_be(&payload[4]);
        chunk_size = read_u16_be(&payload[6]);
        file_crc32 = read_u32_be(&payload[8]);

        if (!legacy_start_session(file_len, chunk_size, chunk_count, file_crc32))
        {
            chunk_size = read_u16_be(&payload[4]);
            chunk_count = read_u16_be(&payload[6]);
            if (legacy_start_session(file_len, chunk_size, chunk_count, file_crc32))
                ack[0] = 1U;
        }
        else
        {
            ack[0] = 1U;
        }
    }

    *code = ack[0] ? 0 : 422;
    *message = ack[0] ? "legacy OTA file info accepted" : "legacy OTA file info invalid";
    *response_data = create_legacy_upgrade_response(SIM_BOARD_LEGACY_CMD_B1_FILE_MSG, ack, sizeof(ack), "file_info");
    return 1U;
}

static uint8_t handle_legacy_b2_file_chunk(const uint8_t *payload,
                                           uint16_t payload_len,
                                           cJSON **response_data,
                                           int *code,
                                           const char **message)
{
    uint8_t ack[2] = {0U, 1U};
    uint16_t pack_index;
    uint32_t offset;
    uint32_t data_len;

    if (ota_state.break_upgrade_flag)
        ack[1] = 0U;

    if (payload != NULL && payload_len > 2U && ota_state.active && !ota_state.break_upgrade_flag)
    {
        pack_index = read_u16_be(payload);
        data_len = (uint32_t)payload_len - 2U;
        offset = (uint32_t)pack_index * ota_state.chunk_size;
        if (pack_index < ota_state.chunk_count &&
            data_len <= ota_state.chunk_size &&
            offset < ota_state.file_len &&
            (offset + data_len) <= ota_state.file_len &&
            write_chunk(offset, &payload[2], data_len))
        {
            if (!ota_pack_flag_is_set(pack_index))
                ota_state.received_bytes += data_len;
            ota_pack_flag_set(pack_index);
            write_upgrade_handle();
            ack[0] = 1U;
        }
    }

    *code = ack[0] ? 0 : 422;
    *message = ack[0] ? "legacy OTA chunk written" : "legacy OTA chunk rejected";
    *response_data = create_legacy_upgrade_response(SIM_BOARD_LEGACY_CMD_B2_FILE_CHUNK, ack, sizeof(ack), "chunk");
    return 1U;
}

static uint8_t handle_legacy_b3_check_lost(cJSON **response_data, int *code, const char **message)
{
    uint8_t payload[3] = {0xFFU, 0xFFU, 0U};
    uint16_t lost_index = ota_first_lost_pack();
    uint32_t flash_crc;

    write_u16_be(payload, lost_index);
    if (lost_index == 0xFFFFU && ota_state.file_len > 0U)
    {
        flash_crc = crc32_flash(SIM_BOARD_APP2_START_ADDR, ota_state.file_len);
        if (flash_crc == ota_state.file_crc32)
        {
            payload[2] = 1U;
            ota_state.active = 0U;
            ota_state.complete = 1U;
            ota_state.upgrade_state = SIM_BOARD_UPGRADE_STATE_SUCCESS;
            write_upgrade_handle();
        }
        else
        {
            set_last_error("legacy OTA image CRC mismatch");
        }
    }

    *code = payload[2] ? 0 : 409;
    *message = payload[2] ? "legacy OTA image verified" : "legacy OTA has missing chunks or CRC mismatch";
    *response_data = create_legacy_upgrade_response(SIM_BOARD_LEGACY_CMD_B3_CHECK_LOST, payload, sizeof(payload), "check_lost");
    return 1U;
}

static uint8_t handle_legacy_b4_end(cJSON **response_data,
                                    int *code,
                                    const char **message,
                                    uint8_t *reboot_requested)
{
    uint8_t payload[1] = {0U};

    if (ota_state.upgrade_state == SIM_BOARD_UPGRADE_STATE_SUCCESS)
    {
        payload[0] = 1U;
        *reboot_requested = 1U;
    }

    *code = payload[0] ? 0 : 409;
    *message = payload[0] ? "legacy OTA reboot scheduled" : "legacy OTA is not complete";
    *response_data = create_legacy_upgrade_response(SIM_BOARD_LEGACY_CMD_B4_END, payload, sizeof(payload), "end");
    return 1U;
}

static uint8_t handle_legacy_b5_result(cJSON **response_data, int *code, const char **message)
{
    uint8_t payload[4] = {1U, 10U, 2U, 55U};

    *code = 0;
    *message = "legacy OTA version";
    *response_data = create_legacy_upgrade_response(SIM_BOARD_LEGACY_CMD_B5_RESULT, payload, sizeof(payload), "result");
    return 1U;
}

static uint8_t handle_legacy_b6_break(cJSON **response_data, int *code, const char **message)
{
    uint8_t payload[1] = {1U};

    ota_state.break_upgrade_flag = 1U;
    ota_state.active = 0U;
    write_upgrade_handle();

    *code = 0;
    *message = "legacy OTA break accepted";
    *response_data = create_legacy_upgrade_response(SIM_BOARD_LEGACY_CMD_B6_BREAK, payload, sizeof(payload), "break");
    return 1U;
}

void sim_board_ota_init(void)
{
    memset(&ota_state, 0, sizeof(ota_state));
}

void sim_board_ota_append_status(cJSON *object)
{
    cJSON *ota = cJSON_AddObjectToObject(object, "ota");
    if (ota != NULL)
        append_status(ota);
}

uint8_t sim_board_ota_handle_command(cJSON *root,
                                     const char *cmd,
                                     cJSON **response_data,
                                     int *code,
                                     const char **message,
                                     uint8_t *reboot_requested)
{
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    *response_data = NULL;
    *code = 0;
    *message = "ok";
    *reboot_requested = 0U;

    if (strcmp(cmd, "ota_begin") == 0)
        return handle_begin(params, response_data, code, message);
    if (strcmp(cmd, "ota_chunk") == 0)
        return handle_chunk(params, response_data, code, message);
    if (strcmp(cmd, "ota_end") == 0)
        return handle_end(params, response_data, code, message, reboot_requested);
    if (strcmp(cmd, "ota_abort") == 0)
    {
        memset(&ota_state, 0, sizeof(ota_state));
        *response_data = cJSON_CreateObject();
        if (*response_data != NULL)
            append_status(*response_data);
        *message = "OTA session aborted";
        return 1;
    }
    if (strcmp(cmd, "ota_status") == 0)
    {
        *response_data = cJSON_CreateObject();
        if (*response_data != NULL)
            append_status(*response_data);
        *message = "OTA status";
        return 1;
    }

    return 0;
}

uint8_t sim_board_ota_handle_legacy_frame(uint8_t cmd_type,
                                          const uint8_t *payload,
                                          uint16_t payload_len,
                                          cJSON **response_data,
                                          int *code,
                                          const char **message,
                                          uint8_t *reboot_requested)
{
    *response_data = NULL;
    *code = 0;
    *message = "ok";
    *reboot_requested = 0U;

    switch (cmd_type)
    {
    case SIM_BOARD_LEGACY_CMD_B0_START:
        return handle_legacy_b0_start(response_data, code, message);
    case SIM_BOARD_LEGACY_CMD_B1_FILE_MSG:
        return handle_legacy_b1_file_msg(payload, payload_len, response_data, code, message);
    case SIM_BOARD_LEGACY_CMD_B2_FILE_CHUNK:
        return handle_legacy_b2_file_chunk(payload, payload_len, response_data, code, message);
    case SIM_BOARD_LEGACY_CMD_B3_CHECK_LOST:
        return handle_legacy_b3_check_lost(response_data, code, message);
    case SIM_BOARD_LEGACY_CMD_B4_END:
        return handle_legacy_b4_end(response_data, code, message, reboot_requested);
    case SIM_BOARD_LEGACY_CMD_B5_RESULT:
        return handle_legacy_b5_result(response_data, code, message);
    case SIM_BOARD_LEGACY_CMD_B6_BREAK:
        return handle_legacy_b6_break(response_data, code, message);
    default:
        *code = 404;
        *message = "unknown legacy OTA command";
        return 0U;
    }
}

uint8_t sim_board_ota_handle_legacy_payload(uint8_t cmd_type,
                                            const uint8_t *payload,
                                            uint16_t payload_len,
                                            uint8_t *response_payload,
                                            uint16_t response_payload_size,
                                            uint16_t *response_payload_len,
                                            int *code,
                                            const char **message,
                                            uint8_t *reboot_requested)
{
    *code = 0;
    *message = "ok";
    *reboot_requested = 0U;

    if ((response_payload == NULL) || (response_payload_len == NULL))
    {
        *code = 500;
        *message = "legacy OTA response buffer is invalid";
        return 1U;
    }
    *response_payload_len = 0U;

    switch (cmd_type)
    {
    case SIM_BOARD_LEGACY_CMD_B0_START:
        if (response_payload_size < 7U)
        {
            *code = 500;
            *message = "legacy OTA start response buffer is too small";
            return 1U;
        }
        response_payload[0] = 1U;
        response_payload[1] = 10U;
        response_payload[2] = 2U;
        response_payload[3] = 55U;
        response_payload[4] = 'x';
        response_payload[5] = 'm';
        response_payload[6] = 1U;
        *response_payload_len = 7U;
        *message = "legacy OTA info";
        return 1U;

    case SIM_BOARD_LEGACY_CMD_B1_FILE_MSG:
    {
        uint32_t file_len;
        uint16_t chunk_count;
        uint16_t chunk_size;
        uint32_t file_crc32;
        response_payload[0] = 0U;
        if (response_payload_size < 1U)
        {
            *code = 500;
            *message = "legacy OTA file info response buffer is too small";
            return 1U;
        }
        if (payload != NULL && payload_len >= 12U)
        {
            file_len = read_u32_be(&payload[0]);
            chunk_count = read_u16_be(&payload[4]);
            chunk_size = read_u16_be(&payload[6]);
            file_crc32 = read_u32_be(&payload[8]);
            if (!legacy_start_session(file_len, chunk_size, chunk_count, file_crc32))
            {
                chunk_size = read_u16_be(&payload[4]);
                chunk_count = read_u16_be(&payload[6]);
                if (legacy_start_session(file_len, chunk_size, chunk_count, file_crc32))
                    response_payload[0] = 1U;
            }
            else
            {
                response_payload[0] = 1U;
            }
        }
        *response_payload_len = 1U;
        *code = response_payload[0] ? 0 : 422;
        *message = response_payload[0] ? "legacy OTA file info accepted" : "legacy OTA file info invalid";
        return 1U;
    }

    case SIM_BOARD_LEGACY_CMD_B2_FILE_CHUNK:
    {
        uint16_t pack_index;
        uint32_t offset;
        uint32_t data_len;
        if (response_payload_size < 2U)
        {
            *code = 500;
            *message = "legacy OTA chunk response buffer is too small";
            return 1U;
        }
        response_payload[0] = 0U;
        response_payload[1] = ota_state.break_upgrade_flag ? 0U : 1U;
        if (payload != NULL && payload_len > 2U && ota_state.active && !ota_state.break_upgrade_flag)
        {
            pack_index = read_u16_be(payload);
            data_len = (uint32_t)payload_len - 2U;
            offset = (uint32_t)pack_index * ota_state.chunk_size;
            if (pack_index < ota_state.chunk_count &&
                data_len <= ota_state.chunk_size &&
                offset < ota_state.file_len &&
                (offset + data_len) <= ota_state.file_len &&
                write_chunk(offset, &payload[2], data_len))
            {
                if (!ota_pack_flag_is_set(pack_index))
                    ota_state.received_bytes += data_len;
                ota_pack_flag_set(pack_index);
                write_upgrade_handle();
                response_payload[0] = 1U;
            }
        }
        *response_payload_len = 2U;
        *code = response_payload[0] ? 0 : 422;
        *message = response_payload[0] ? "legacy OTA chunk written" : "legacy OTA chunk rejected";
        return 1U;
    }

    case SIM_BOARD_LEGACY_CMD_B3_CHECK_LOST:
    {
        uint16_t lost_index;
        uint32_t flash_crc;
        if (response_payload_size < 3U)
        {
            *code = 500;
            *message = "legacy OTA check response buffer is too small";
            return 1U;
        }
        lost_index = ota_first_lost_pack();
        write_u16_be(response_payload, lost_index);
        response_payload[2] = 0U;
        if (lost_index == 0xFFFFU && ota_state.file_len > 0U)
        {
            flash_crc = crc32_flash(SIM_BOARD_APP2_START_ADDR, ota_state.file_len);
            if (flash_crc == ota_state.file_crc32)
            {
                response_payload[2] = 1U;
                ota_state.active = 0U;
                ota_state.complete = 1U;
                ota_state.upgrade_state = SIM_BOARD_UPGRADE_STATE_SUCCESS;
                write_upgrade_handle();
            }
            else
            {
                set_last_error("legacy OTA image CRC mismatch");
            }
        }
        *response_payload_len = 3U;
        *code = response_payload[2] ? 0 : 409;
        *message = response_payload[2] ? "legacy OTA image verified" : "legacy OTA has missing chunks or CRC mismatch";
        return 1U;
    }

    case SIM_BOARD_LEGACY_CMD_B4_END:
        if (response_payload_size < 1U)
        {
            *code = 500;
            *message = "legacy OTA end response buffer is too small";
            return 1U;
        }
        response_payload[0] = 0U;
        if (ota_state.upgrade_state == SIM_BOARD_UPGRADE_STATE_SUCCESS)
        {
            response_payload[0] = 1U;
            *reboot_requested = 1U;
        }
        *response_payload_len = 1U;
        *code = response_payload[0] ? 0 : 409;
        *message = response_payload[0] ? "legacy OTA reboot scheduled" : "legacy OTA is not complete";
        return 1U;

    case SIM_BOARD_LEGACY_CMD_B5_RESULT:
        if (response_payload_size < 4U)
        {
            *code = 500;
            *message = "legacy OTA result response buffer is too small";
            return 1U;
        }
        response_payload[0] = 1U;
        response_payload[1] = 10U;
        response_payload[2] = 2U;
        response_payload[3] = 55U;
        *response_payload_len = 4U;
        *message = "legacy OTA version";
        return 1U;

    case SIM_BOARD_LEGACY_CMD_B6_BREAK:
        if (response_payload_size < 1U)
        {
            *code = 500;
            *message = "legacy OTA break response buffer is too small";
            return 1U;
        }
        ota_state.break_upgrade_flag = 1U;
        ota_state.active = 0U;
        write_upgrade_handle();
        response_payload[0] = 1U;
        *response_payload_len = 1U;
        *message = "legacy OTA break accepted";
        return 1U;

    default:
        *code = 404;
        *message = "unknown legacy OTA command";
        return 0U;
    }
}
