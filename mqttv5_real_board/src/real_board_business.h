#ifndef REAL_BOARD_BUSINESS_H
#define REAL_BOARD_BUSINESS_H

#include "stdint.h"
#include "cJSON.h"

#define REAL_BOARD_FRM_PREAMBLE 0xFEU
#define REAL_BOARD_FRM_TYPE_MAINBOARD_PARA_SET 0xA1U
#define REAL_BOARD_FRM_TYPE_MAINBOARD_CONTROL 0xA2U
#define REAL_BOARD_FRM_TYPE_MAINBOARD_DEBUG_MSG 0xA3U
#define REAL_BOARD_FRM_TYPE_MAINBOARD_UPGRADE 0xA4U
#define REAL_BOARD_FRM_TYPE_OUTSIDE_DEVICE 0xA5U

#define REAL_BOARD_CMD_A1_READ_CPU_ID 0x09U
#define REAL_BOARD_CMD_A1_SET_PRODUCT_KEY 0x0AU
#define REAL_BOARD_CMD_A1_SET_DEVICE_NAME 0x0BU
#define REAL_BOARD_CMD_A1_SET_DEVICE_SECRET 0x0CU
#define REAL_BOARD_CMD_A1_SET_ENDPOINT 0xFAU
#define REAL_BOARD_CMD_A1_SET_KNS 0xFBU
#define REAL_BOARD_CMD_A2_RESET 0x01U
#define REAL_BOARD_A5_PENDING_MAX 4U
#define REAL_BOARD_A5_RESPONSE_MAX 96U

#ifndef REAL_BOARD_FW_VERSION
#define REAL_BOARD_FW_VERSION "real-board-app1"
#endif

#ifndef REAL_BOARD_ENABLE_LEGACY_FRAME
#define REAL_BOARD_ENABLE_LEGACY_FRAME 1
#endif

typedef struct
{
    uint8_t valid;
    uint32_t voltage_mv;
    uint32_t current_ma;
    uint32_t active_power_mw;
    uint32_t reactive_power_mvar;
    uint32_t power_factor_x1000;
    uint32_t energy_one_wh;
    uint32_t energy_total_wh;
    uint32_t pulse_count;
} real_board_meter_hlw8112_t;

typedef struct
{
    uint8_t valid;
    int16_t temperature_c_x10;
    uint16_t humidity_rh_x10;
    uint16_t pm25_ugm3;
    uint16_t co2_ppm;
    uint32_t illuminance_lux;
} real_board_environment_t;

typedef struct
{
    uint8_t valid;
    uint8_t online;
    uint8_t device_count;
    uint16_t last_response_ms;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t error_count;
} real_board_rs485_t;

void real_board_business_init(void);
void real_board_business_tick(void);
void real_board_business_update_meter_hlw8112(const real_board_meter_hlw8112_t *meter);
void real_board_business_update_environment(const real_board_environment_t *environment);
void real_board_business_update_rs485(const real_board_rs485_t *rs485);
uint8_t real_board_business_handle_peripheral_response(uint8_t cmd_type, const uint8_t *payload, uint16_t payload_len);
void real_board_business_append_telemetry(cJSON *root);
void real_board_business_append_status(cJSON *object);
uint8_t real_board_business_handle_json_command(cJSON *root,
                                                const char *cmd,
                                                cJSON **response_data,
                                                int *code,
                                                const char **message);
uint8_t real_board_business_publish_event(cJSON *root);
uint8_t real_board_business_handle_legacy_command(cJSON *root,
                                                  const char *cmd,
                                                  cJSON **response_data,
                                                  int *code,
                                                  const char **message,
                                                  uint8_t *reboot_requested);

#endif
