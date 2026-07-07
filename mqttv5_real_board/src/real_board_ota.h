#ifndef REAL_BOARD_OTA_H
#define REAL_BOARD_OTA_H

#include "stdint.h"
#include "cJSON.h"

#define REAL_BOARD_FLASH_WRITE_END_ADDR 0x00080000UL
#define REAL_BOARD_FLASH_PAGE_SIZE 0x00002000UL
#define REAL_BOARD_APP1_START_ADDR 0x00004000UL
#define REAL_BOARD_APP2_START_ADDR 0x00042000UL
#define REAL_BOARD_APP2_END_ADDR 0x00076000UL
#define REAL_BOARD_APP2_SECTOR_COUNT ((REAL_BOARD_APP2_END_ADDR - REAL_BOARD_APP2_START_ADDR) / REAL_BOARD_FLASH_PAGE_SIZE)
#define REAL_BOARD_UPGRADE_STATE_ADDR 0x0007A000UL
#define REAL_BOARD_UPGRADE_STATE_SUCCESS 0xA55AU
#define REAL_BOARD_OTA_PACK_FLAG_BYTES 128U
#define REAL_BOARD_OTA_MAX_CHUNKS 1024U
#define REAL_BOARD_OTA_MAX_CHUNK_SIZE 1024U

#ifndef REAL_BOARD_OTA_REBOOT_AFTER_END
#define REAL_BOARD_OTA_REBOOT_AFTER_END 1
#endif

#pragma pack(1)
typedef struct
{
    uint32_t file_len;
    uint16_t frm_total;
    uint16_t frm_data_len;
    uint32_t file_crc;
} real_board_ota_file_msg_t;

typedef struct
{
    real_board_ota_file_msg_t file_msg;
    uint16_t upgrade_state;
    uint8_t break_upgrade_flag;
    uint8_t retain;
    uint8_t pack_flag[REAL_BOARD_OTA_PACK_FLAG_BYTES];
} real_board_ota_legacy_handle_t;
#pragma pack()

void real_board_ota_init(void);
void real_board_ota_append_status(cJSON *object);
uint8_t real_board_ota_handle_command(cJSON *root,
                                     const char *cmd,
                                     cJSON **response_data,
                                     int *code,
                                     const char **message,
                                     uint8_t *reboot_requested);
uint8_t real_board_ota_handle_legacy_payload(uint8_t cmd_type,
                                             const uint8_t *payload,
                                             uint16_t payload_len,
                                             uint8_t *response_payload,
                                             uint16_t response_payload_size,
                                             uint16_t *response_payload_len,
                                             int *code,
                                             const char **message,
                                             uint8_t *reboot_requested);

#endif



