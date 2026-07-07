#include "real_board_ota.h"

#include "driver.h"
#include "mbedtls/base64.h"
#include "stdio.h"
#include "string.h"

#define REAL_BOARD_OTA_BASE64_MAX 1500U
#define REAL_BOARD_OTA_LEGACY_CHUNK_INDEX_BYTES 2U
#define REAL_BOARD_OTA_LEGACY_HW_MAJOR 1U
#define REAL_BOARD_OTA_LEGACY_HW_MINOR 10U
#define REAL_BOARD_OTA_LEGACY_SW_MAJOR 2U
#define REAL_BOARD_OTA_LEGACY_SW_MINOR 55U

typedef struct
{
    uint8_t active;
    uint8_t complete;
    char session_id[40];
    uint32_t file_len;
    uint32_t file_crc32;
    uint16_t chunk_size;
    uint16_t chunk_count;
    uint32_t received_bytes;
    uint8_t pack_flag[REAL_BOARD_OTA_PACK_FLAG_BYTES];
    uint8_t sector_erased[REAL_BOARD_APP2_SECTOR_COUNT];
    char last_error[80];
} real_board_ota_state_t;

static real_board_ota_state_t ota_state;

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
    if (index >= REAL_BOARD_OTA_MAX_CHUNKS)
        return;
    ota_state.pack_flag[index / 8U] |= (uint8_t)(1U << (index % 8U));
}

static uint8_t ota_pack_flag_is_set(uint16_t index)
{
    if (index >= REAL_BOARD_OTA_MAX_CHUNKS)
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
    cJSON_AddNumberToObject(object, "app1_start", REAL_BOARD_APP1_START_ADDR);
    cJSON_AddNumberToObject(object, "app2_start", REAL_BOARD_APP2_START_ADDR);
    cJSON_AddNumberToObject(object, "app2_end", REAL_BOARD_APP2_END_ADDR);
    cJSON_AddNumberToObject(object, "upgrade_state_addr", REAL_BOARD_UPGRADE_STATE_ADDR);
    cJSON_AddNumberToObject(object, "file_len", ota_state.file_len);
    cJSON_AddNumberToObject(object, "file_crc32", ota_state.file_crc32);
    cJSON_AddNumberToObject(object, "chunk_size", ota_state.chunk_size);
    cJSON_AddNumberToObject(object, "chunk_count", ota_state.chunk_count);
    cJSON_AddNumberToObject(object, "received_bytes", ota_state.received_bytes);
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

static uint16_t read_u16_be(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
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

static uint8_t session_matches(const char *session_id)
{
    return (ota_state.active || ota_state.complete) && (strcmp(session_id, ota_state.session_id) == 0);
}

static uint8_t ensure_sector_erased(uint32_t addr, uint32_t end_addr)
{
    uint32_t first_sector;
    uint32_t last_sector;

    if ((addr < REAL_BOARD_APP2_START_ADDR) || (end_addr > REAL_BOARD_APP2_END_ADDR) || (addr >= end_addr))
        return 0;

    first_sector = (addr - REAL_BOARD_APP2_START_ADDR) / REAL_BOARD_FLASH_PAGE_SIZE;
    last_sector = (end_addr - REAL_BOARD_APP2_START_ADDR - 1U) / REAL_BOARD_FLASH_PAGE_SIZE;
    if (last_sector >= REAL_BOARD_APP2_SECTOR_COUNT)
        return 0;

    for (uint32_t sector = first_sector; sector <= last_sector; sector++)
    {
        if (ota_state.sector_erased[sector] == 0U)
        {
            flash_erase_sector(REAL_BOARD_APP2_START_ADDR + (sector * REAL_BOARD_FLASH_PAGE_SIZE));
            ota_state.sector_erased[sector] = 1U;
        }
    }
    return 1;
}

static uint8_t write_chunk(uint32_t offset, const uint8_t *data, uint32_t data_len)
{
    uint8_t write_buf[REAL_BOARD_OTA_MAX_CHUNK_SIZE + 4U];
    uint8_t verify_buf[REAL_BOARD_OTA_MAX_CHUNK_SIZE + 4U];
    uint32_t addr = REAL_BOARD_APP2_START_ADDR + offset;
    uint32_t write_len = (data_len + 3U) & ~3U;
    uint32_t end_addr = addr + write_len;

    if ((addr < REAL_BOARD_APP2_START_ADDR) || (end_addr > REAL_BOARD_APP2_END_ADDR))
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
    if (ota_state.chunk_count == 0U || ota_state.chunk_count > REAL_BOARD_OTA_MAX_CHUNKS)
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
    real_board_ota_legacy_handle_t handle;
    uint8_t write_buf[sizeof(real_board_ota_legacy_handle_t) + 4U];
    uint32_t write_len = (sizeof(real_board_ota_legacy_handle_t) + 3U) & ~3U;

    memset(&handle, 0, sizeof(handle));
    handle.file_msg.file_len = ota_state.file_len;
    handle.file_msg.frm_total = ota_state.chunk_count;
    handle.file_msg.frm_data_len = ota_state.chunk_size;
    handle.file_msg.file_crc = ota_state.file_crc32;
    handle.upgrade_state = REAL_BOARD_UPGRADE_STATE_SUCCESS;
    memcpy(handle.pack_flag, ota_state.pack_flag, sizeof(handle.pack_flag));

    memset(write_buf, 0xFFU, write_len);
    memcpy(write_buf, &handle, sizeof(handle));
    flash_erase_sector(REAL_BOARD_UPGRADE_STATE_ADDR);
    flash_write(REAL_BOARD_UPGRADE_STATE_ADDR, write_buf, (uint16_t)write_len);
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

    if (file_len == 0U || file_len > (REAL_BOARD_APP2_END_ADDR - REAL_BOARD_APP2_START_ADDR))
    {
        *code = 422;
        *message = "OTA image length exceeds APP2";
        return 1;
    }
    if (chunk_size == 0U || chunk_size > REAL_BOARD_OTA_MAX_CHUNK_SIZE || chunk_count == 0U || chunk_count > REAL_BOARD_OTA_MAX_CHUNKS)
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
    uint8_t decode_buf[REAL_BOARD_OTA_MAX_CHUNK_SIZE];
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
    if (index >= ota_state.chunk_count || index >= REAL_BOARD_OTA_MAX_CHUNKS || offset >= ota_state.file_len)
    {
        *code = 422;
        *message = "OTA chunk index or offset is invalid";
        return 1;
    }
    if (strlen(base64_data) > REAL_BOARD_OTA_BASE64_MAX ||
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
    flash_crc = crc32_flash(REAL_BOARD_APP2_START_ADDR, ota_state.file_len);
    if (flash_crc != ota_state.file_crc32)
    {
        set_last_error("OTA image CRC mismatch");
        *code = 422;
        *message = ota_state.last_error;
        return 1;
    }

    write_upgrade_handle();
    ota_state.active = 0U;
    ota_state.complete = 1U;
    *reboot_requested = REAL_BOARD_OTA_REBOOT_AFTER_END ? 1U : 0U;

    *response_data = cJSON_CreateObject();
    if (*response_data != NULL)
        append_transfer_ack(*response_data, "end", 0xFFFFFFFFUL);
    *message = REAL_BOARD_OTA_REBOOT_AFTER_END ? "OTA image verified; reboot scheduled" : "OTA image verified; reboot deferred for host readback";
    return 1;
}

static uint8_t legacy_ota_handle_b0_start(uint8_t *response_payload,
                                          uint16_t response_payload_size,
                                          uint16_t *response_payload_len,
                                          int *code,
                                          const char **message)
{
    if (response_payload_size < 7U)
    {
        *code = 500;
        *message = "legacy OTA response buffer is too small";
        return 1;
    }

    response_payload[0] = REAL_BOARD_OTA_LEGACY_HW_MAJOR;
    response_payload[1] = REAL_BOARD_OTA_LEGACY_HW_MINOR;
    response_payload[2] = REAL_BOARD_OTA_LEGACY_SW_MAJOR;
    response_payload[3] = REAL_BOARD_OTA_LEGACY_SW_MINOR;
    response_payload[4] = 'x';
    response_payload[5] = 'm';
    response_payload[6] = 0x01U;
    *response_payload_len = 7U;
    *message = "legacy OTA start accepted";
    return 1;
}

static uint8_t legacy_ota_handle_b1_file_msg(const uint8_t *payload,
                                             uint16_t payload_len,
                                             uint8_t *response_payload,
                                             uint16_t response_payload_size,
                                             uint16_t *response_payload_len,
                                             int *code,
                                             const char **message)
{
    uint32_t file_len;
    uint16_t chunk_count;
    uint16_t chunk_size;
    uint32_t file_crc;
    uint16_t expected_chunks;

    if ((payload == NULL) || (payload_len < 12U) || (response_payload_size < 1U))
    {
        *code = 422;
        *message = "legacy OTA file message is invalid";
        return 1;
    }

    file_len = read_u32_be(&payload[0]);
    chunk_count = read_u16_be(&payload[4]);
    chunk_size = read_u16_be(&payload[6]);
    file_crc = read_u32_be(&payload[8]);
    if ((file_len == 0U) ||
        (file_len > (REAL_BOARD_APP2_END_ADDR - REAL_BOARD_APP2_START_ADDR)) ||
        (chunk_count == 0U) ||
        (chunk_count > REAL_BOARD_OTA_MAX_CHUNKS) ||
        (chunk_size == 0U) ||
        (chunk_size > REAL_BOARD_OTA_MAX_CHUNK_SIZE))
    {
        chunk_size = read_u16_be(&payload[4]);
        chunk_count = read_u16_be(&payload[6]);
        if ((file_len == 0U) ||
            (file_len > (REAL_BOARD_APP2_END_ADDR - REAL_BOARD_APP2_START_ADDR)) ||
            (chunk_count == 0U) ||
            (chunk_count > REAL_BOARD_OTA_MAX_CHUNKS) ||
            (chunk_size == 0U) ||
            (chunk_size > REAL_BOARD_OTA_MAX_CHUNK_SIZE))
        {
            response_payload[0] = 0x00U;
            *response_payload_len = 1U;
            *code = 422;
            *message = "legacy OTA file settings are invalid";
            return 1;
        }
    }

    expected_chunks = (uint16_t)((file_len + chunk_size - 1U) / chunk_size);
    if (expected_chunks != chunk_count)
    {
        chunk_size = read_u16_be(&payload[4]);
        chunk_count = read_u16_be(&payload[6]);
        if ((chunk_size == 0U) ||
            (chunk_size > REAL_BOARD_OTA_MAX_CHUNK_SIZE) ||
            (chunk_count == 0U) ||
            (chunk_count > REAL_BOARD_OTA_MAX_CHUNKS))
        {
            response_payload[0] = 0x00U;
            *response_payload_len = 1U;
            *code = 422;
            *message = "legacy OTA chunk count mismatch";
            return 1;
        }
        expected_chunks = (uint16_t)((file_len + chunk_size - 1U) / chunk_size);
        if (expected_chunks != chunk_count)
        {
            response_payload[0] = 0x00U;
            *response_payload_len = 1U;
            *code = 422;
            *message = "legacy OTA chunk count mismatch";
            return 1;
        }
    }

    memset(&ota_state, 0, sizeof(ota_state));
    ota_state.active = 1U;
    snprintf(ota_state.session_id, sizeof(ota_state.session_id), "%s", "legacy");
    ota_state.file_len = file_len;
    ota_state.file_crc32 = file_crc;
    ota_state.chunk_size = chunk_size;
    ota_state.chunk_count = chunk_count;

    response_payload[0] = 0x01U;
    *response_payload_len = 1U;
    *message = "legacy OTA file message accepted";
    return 1;
}

static uint8_t legacy_ota_handle_b2_chunk(const uint8_t *payload,
                                          uint16_t payload_len,
                                          uint8_t *response_payload,
                                          uint16_t response_payload_size,
                                          uint16_t *response_payload_len,
                                          int *code,
                                          const char **message)
{
    uint16_t index;
    uint32_t offset;
    uint16_t data_len;

    if ((payload == NULL) ||
        (payload_len <= REAL_BOARD_OTA_LEGACY_CHUNK_INDEX_BYTES) ||
        (response_payload_size < 4U))
    {
        *code = 422;
        *message = "legacy OTA chunk is invalid";
        return 1;
    }
    if (!ota_state.active || ota_state.chunk_size == 0U)
    {
        *code = 409;
        *message = "legacy OTA session is not active";
        return 1;
    }

    index = read_u16_be(payload);
    data_len = (uint16_t)(payload_len - REAL_BOARD_OTA_LEGACY_CHUNK_INDEX_BYTES);
    offset = (uint32_t)index * ota_state.chunk_size;
    if ((index >= ota_state.chunk_count) ||
        (data_len > ota_state.chunk_size) ||
        (offset >= ota_state.file_len) ||
        ((offset + data_len) > ota_state.file_len))
    {
        *code = 422;
        *message = "legacy OTA chunk index or length is invalid";
        return 1;
    }
    if (!write_chunk(offset, &payload[REAL_BOARD_OTA_LEGACY_CHUNK_INDEX_BYTES], data_len))
    {
        *code = 500;
        *message = ota_state.last_error;
        return 1;
    }

    if (!ota_pack_flag_is_set(index))
        ota_state.received_bytes += data_len;
    ota_pack_flag_set(index);

    response_payload[0] = payload[0];
    response_payload[1] = payload[1];
    response_payload[2] = 0x01U;
    response_payload[3] = 0x00U;
    *response_payload_len = 4U;
    *message = "legacy OTA chunk written";
    return 1;
}

static uint8_t legacy_ota_handle_b3_check(uint8_t *response_payload,
                                          uint16_t response_payload_size,
                                          uint16_t *response_payload_len,
                                          int *code,
                                          const char **message)
{
    uint16_t cursor = 3U;
    uint16_t missing = 0U;
    uint32_t flash_crc;

    if (response_payload_size < 3U)
    {
        *code = 500;
        *message = "legacy OTA response buffer is too small";
        return 1;
    }
    if (ota_state.chunk_count == 0U)
    {
        write_u16_be(response_payload, 0U);
        response_payload[2] = 0x00U;
        *response_payload_len = 3U;
        *code = 409;
        *message = "legacy OTA session is not active";
        return 1;
    }

    for (uint16_t i = 0; i < ota_state.chunk_count; i++)
    {
        if (!ota_pack_flag_is_set(i))
        {
            if ((cursor + 2U) <= response_payload_size)
            {
                write_u16_be(&response_payload[cursor], i);
                cursor += 2U;
            }
            missing++;
        }
    }

    write_u16_be(response_payload, missing);
    response_payload[2] = 0x00U;
    if (missing == 0U)
    {
        flash_crc = crc32_flash(REAL_BOARD_APP2_START_ADDR, ota_state.file_len);
        if (flash_crc == ota_state.file_crc32)
        {
            response_payload[2] = 0x01U;
            write_upgrade_handle();
            ota_state.active = 0U;
            ota_state.complete = 1U;
            *message = "legacy OTA image verified";
        }
        else
        {
            set_last_error("legacy OTA image CRC mismatch");
            *code = 422;
            *message = ota_state.last_error;
        }
    }
    else
    {
        *code = 409;
        *message = "legacy OTA chunks are incomplete";
    }

    *response_payload_len = cursor;
    return 1;
}

static uint8_t legacy_ota_handle_b4_end(uint8_t *response_payload,
                                        uint16_t response_payload_size,
                                        uint16_t *response_payload_len,
                                        int *code,
                                        const char **message,
                                        uint8_t *reboot_requested)
{
    if (response_payload_size < 1U)
    {
        *code = 500;
        *message = "legacy OTA response buffer is too small";
        return 1;
    }

    if (ota_state.complete)
    {
        response_payload[0] = 0x01U;
        *reboot_requested = REAL_BOARD_OTA_REBOOT_AFTER_END ? 1U : 0U;
        *message = REAL_BOARD_OTA_REBOOT_AFTER_END ? "legacy OTA complete; reboot scheduled" : "legacy OTA complete; reboot deferred";
    }
    else
    {
        response_payload[0] = 0x00U;
        *code = 409;
        *message = "legacy OTA image is not verified";
    }
    *response_payload_len = 1U;
    return 1;
}

static uint8_t legacy_ota_handle_b5_result(uint8_t *response_payload,
                                           uint16_t response_payload_size,
                                           uint16_t *response_payload_len,
                                           int *code,
                                           const char **message)
{
    if (response_payload_size < 4U)
    {
        *code = 500;
        *message = "legacy OTA response buffer is too small";
        return 1;
    }

    response_payload[0] = REAL_BOARD_OTA_LEGACY_HW_MAJOR;
    response_payload[1] = REAL_BOARD_OTA_LEGACY_HW_MINOR;
    response_payload[2] = REAL_BOARD_OTA_LEGACY_SW_MAJOR;
    response_payload[3] = REAL_BOARD_OTA_LEGACY_SW_MINOR;
    *response_payload_len = 4U;
    *message = "legacy OTA result returned";
    return 1;
}

static uint8_t legacy_ota_handle_b6_abort(uint8_t *response_payload,
                                          uint16_t response_payload_size,
                                          uint16_t *response_payload_len,
                                          int *code,
                                          const char **message)
{
    if (response_payload_size < 1U)
    {
        *code = 500;
        *message = "legacy OTA response buffer is too small";
        return 1;
    }

    ota_state.active = 0U;
    response_payload[0] = 0x01U;
    *response_payload_len = 1U;
    *message = "legacy OTA aborted";
    return 1;
}

void real_board_ota_init(void)
{
    memset(&ota_state, 0, sizeof(ota_state));
}

void real_board_ota_append_status(cJSON *object)
{
    cJSON *ota = cJSON_AddObjectToObject(object, "ota");
    if (ota != NULL)
        append_status(ota);
}

uint8_t real_board_ota_handle_command(cJSON *root,
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

uint8_t real_board_ota_handle_legacy_payload(uint8_t cmd_type,
                                             const uint8_t *payload,
                                             uint16_t payload_len,
                                             uint8_t *response_payload,
                                             uint16_t response_payload_size,
                                             uint16_t *response_payload_len,
                                             int *code,
                                             const char **message,
                                             uint8_t *reboot_requested)
{
    *response_payload_len = 0U;
    *code = 0;
    *message = "ok";
    *reboot_requested = 0U;

    switch (cmd_type)
    {
    case 0xB0U:
        (void)payload;
        (void)payload_len;
        return legacy_ota_handle_b0_start(response_payload, response_payload_size, response_payload_len, code, message);
    case 0xB1U:
        return legacy_ota_handle_b1_file_msg(payload, payload_len, response_payload, response_payload_size, response_payload_len, code, message);
    case 0xB2U:
        return legacy_ota_handle_b2_chunk(payload, payload_len, response_payload, response_payload_size, response_payload_len, code, message);
    case 0xB3U:
        (void)payload;
        (void)payload_len;
        return legacy_ota_handle_b3_check(response_payload, response_payload_size, response_payload_len, code, message);
    case 0xB4U:
        (void)payload;
        (void)payload_len;
        return legacy_ota_handle_b4_end(response_payload, response_payload_size, response_payload_len, code, message, reboot_requested);
    case 0xB5U:
        (void)payload;
        (void)payload_len;
        return legacy_ota_handle_b5_result(response_payload, response_payload_size, response_payload_len, code, message);
    case 0xB6U:
        (void)payload;
        (void)payload_len;
        return legacy_ota_handle_b6_abort(response_payload, response_payload_size, response_payload_len, code, message);
    default:
        return 0;
    }
}



