#ifndef SIM_BOARD_BUSINESS_H
#define SIM_BOARD_BUSINESS_H

#include "stdint.h"
#include "cJSON.h"

#define SIM_BOARD_FRM_PREAMBLE 0xFEU
#define SIM_BOARD_FRM_TYPE_MAINBOARD_PARA_SET 0xA1U
#define SIM_BOARD_FRM_TYPE_MAINBOARD_CONTROL 0xA2U
#define SIM_BOARD_FRM_TYPE_MAINBOARD_DEBUG_MSG 0xA3U
#define SIM_BOARD_FRM_TYPE_MAINBOARD_UPGRADE 0xA4U
#define SIM_BOARD_FRM_TYPE_OUTSIDE_DEVICE 0xA5U

#define SIM_BOARD_CMD_A1_READ_CPU_ID 0x09U
#define SIM_BOARD_CMD_A1_SET_PRODUCT_KEY 0x0AU
#define SIM_BOARD_CMD_A1_SET_DEVICE_NAME 0x0BU
#define SIM_BOARD_CMD_A1_SET_DEVICE_SECRET 0x0CU
#define SIM_BOARD_CMD_A1_SET_ENDPOINT 0xFAU
#define SIM_BOARD_CMD_A1_SET_KNS 0xFBU
#define SIM_BOARD_CMD_A2_RESET 0x01U
#define SIM_BOARD_CMD_A5_DEVICE_TYPE_1 0x01U

void sim_board_business_init(void);
void sim_board_business_tick(void);
void sim_board_business_append_telemetry(cJSON *root);
void sim_board_business_append_status(cJSON *object);
uint8_t sim_board_business_handle_json_command(cJSON *root,
                                               const char *cmd,
                                               cJSON **response_data,
                                               int *code,
                                               const char **message);
uint8_t sim_board_business_publish_periodic_event(cJSON *root);
uint8_t sim_board_business_handle_legacy_command(cJSON *root,
                                                  const char *cmd,
                                                  cJSON **response_data,
                                                  int *code,
                                                  const char **message,
                                                  uint8_t *reboot_requested);

#endif

