#include "config.h"
#include "driver.h"
#include "stddef.h"
#include "stdio.h"
#include "string.h"

#define FLASH_WRITE_END_ADDR 0x00080000UL
#define FLASH_PAGE_SIZE 0x00002000UL
#define SYSTEM_CONFIG_FLASH_ADDR (FLASH_WRITE_END_ADDR - (5UL * FLASH_PAGE_SIZE))
#define SYSTEM_CONFIG_LEGACY_FLASH_ADDR (FLASH_WRITE_END_ADDR - (2UL * FLASH_PAGE_SIZE))
#define SYSTEM_CONFIG_FLASH_SIZE FLASH_PAGE_SIZE

#ifndef SIM_BOARD_DEFAULT_MQTT_HOST
#define SIM_BOARD_DEFAULT_MQTT_HOST "192.168.0.110"
#endif

#ifndef SIM_BOARD_DEFAULT_MQTT_PORT
#define SIM_BOARD_DEFAULT_MQTT_PORT 1883
#endif

#ifndef SIM_BOARD_DEFAULT_MQTT_USERNAME
#define SIM_BOARD_DEFAULT_MQTT_USERNAME ""
#endif

#ifndef SIM_BOARD_DEFAULT_MQTT_PASSWORD
#define SIM_BOARD_DEFAULT_MQTT_PASSWORD ""
#endif

#ifndef SIM_BOARD_DEFAULT_NTP_SERVER
#define SIM_BOARD_DEFAULT_NTP_SERVER "pool.ntp.org"
#endif

#ifndef SIM_BOARD_DEFAULT_DEVICE_TYPE_NAME
#define SIM_BOARD_DEFAULT_DEVICE_TYPE_NAME "GM400"
#endif

#ifndef SIM_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX
#define SIM_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX "v1/devices/request/"
#endif

#ifndef SIM_BOARD_DEFAULT_RESPONSE_TOPIC_PREFIX
#define SIM_BOARD_DEFAULT_RESPONSE_TOPIC_PREFIX "v1/devices/response/"
#endif

system_config_t *system_config = (system_config_t *)SYSTEM_CONFIG_FLASH_ADDR;
system_config_t system_config_temp;

const system_config_t system_config_def = {
    .eth.mode = 1,
    .eth.ip = {192, 168, 0, 30},
    .eth.gw = {192, 168, 0, 1},
    .eth.sn = {255, 255, 255, 0},
    .eth.dns = {8, 8, 8, 8},
    .device_id = SIM_BOARD_DEFAULT_DEVICE_TYPE_NAME,

    .mqtt_server.host = SIM_BOARD_DEFAULT_MQTT_HOST,
    .mqtt_server.port = SIM_BOARD_DEFAULT_MQTT_PORT,
    .mqtt_server.username = SIM_BOARD_DEFAULT_MQTT_USERNAME,
    .mqtt_server.password = SIM_BOARD_DEFAULT_MQTT_PASSWORD,
    .mqtt_server.topics = {
        "v1/devices/response/{device_name}",
        "v1/devices/request/{device_name}",
        "v1/devices/response/{device_name}",
        "v1/devices/request/{device_name}",
        "v1/devices/response/{device_name}",
    },
    .mqtt_server.qos = {2, 2, 2, 2, 2},
    .mqtt_server.ntp_server = SIM_BOARD_DEFAULT_NTP_SERVER};

static uint32_t config_calculate_crc(system_config_t *config)
{
    crc_reset();
    return crc_block_calculate((uint8_t *)config, offsetof(system_config_t, crc));
}

static uint8_t config_is_valid(system_config_t *config)
{
    if (config->magic != SYSTEM_CONFIG_MAGIC)
        return 0;
    if (config->version != SYSTEM_CONFIG_VERSION)
        return 0;
    if (config->eth.mode > 2)
        return 0;
    if (config->mqtt_server.port == 0)
        return 0;
    if (config->mqtt_server.host[0] == '\0')
        return 0;
    if (config->mqtt_server.topics[0][0] == '\0')
        return 0;
    return config->crc == config_calculate_crc(config);
}

uint64_t EFM_GetUUID()
{
    uint8_t serial_num[8];
    *(uint32_t *)&serial_num[0] = CM_EFM->UQID0;
    *(uint8_t *)&serial_num[4] = (CM_EFM->UQID1 >> 16) & 0xFF;
    *(uint8_t *)&serial_num[5] = (CM_EFM->UQID1 >> 0) & 0xFF;
    *(uint8_t *)&serial_num[6] = (CM_EFM->UQID2 >> 8) & 0xFF;
    *(uint8_t *)&serial_num[7] = (CM_EFM->UQID2 >> 0) & 0xFF;

    return *(uint64_t *)serial_num;
}

void get_mac(uint8_t *mac)
{
    uint64_t mac_temp = EFM_GetUUID();
    // 从第2个字节开始复制6字节
    memcpy(mac, (uint8_t *)&mac_temp + 2, 6);
    // 确保是单播本地管理地址
    mac[0] &= 0xFE; // 清除多播位
    mac[0] |= 0x02; // 设置为本地管理
}

void config_save(system_config_t *system_config_in)
{
    system_config_in->magic = SYSTEM_CONFIG_MAGIC;
    system_config_in->version = SYSTEM_CONFIG_VERSION;
    system_config_in->crc = config_calculate_crc(system_config_in);
    flash_erase_sector(SYSTEM_CONFIG_FLASH_ADDR);
    flash_write(SYSTEM_CONFIG_FLASH_ADDR, (uint8_t *)system_config_in, sizeof(system_config_t));
}

void config_reset()
{
    char request_topic[MQTT_TOPIC_LEN];
    char response_topic[MQTT_TOPIC_LEN];

    system_config_temp = system_config_def;
    get_mac(system_config_temp.eth.mac);
    snprintf(system_config_temp.device_id, sizeof(system_config_temp.device_id), "%s-%02X%02X%02X",
             SIM_BOARD_DEFAULT_DEVICE_TYPE_NAME,
             system_config_temp.eth.mac[3], system_config_temp.eth.mac[4], system_config_temp.eth.mac[5]);
    snprintf(request_topic, sizeof(request_topic), "%s%s", SIM_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX, system_config_temp.device_id);
    snprintf(response_topic, sizeof(response_topic), "%s%s", SIM_BOARD_DEFAULT_RESPONSE_TOPIC_PREFIX, system_config_temp.device_id);
    memset(system_config_temp.mqtt_server.topics[0], 0, MQTT_TOPIC_LEN);
    strcpy(system_config_temp.mqtt_server.topics[0], response_topic);
    memset(system_config_temp.mqtt_server.topics[1], 0, MQTT_TOPIC_LEN);
    strcpy(system_config_temp.mqtt_server.topics[1], request_topic);
    memset(system_config_temp.mqtt_server.topics[2], 0, MQTT_TOPIC_LEN);
    strcpy(system_config_temp.mqtt_server.topics[2], response_topic);
    memset(system_config_temp.mqtt_server.topics[3], 0, MQTT_TOPIC_LEN);
    strcpy(system_config_temp.mqtt_server.topics[3], request_topic);
    memset(system_config_temp.mqtt_server.topics[4], 0, MQTT_TOPIC_LEN);
    strcpy(system_config_temp.mqtt_server.topics[4], response_topic);
    config_save(&system_config_temp);
}

void config_check()
{
    if (config_is_valid((system_config_t *)SYSTEM_CONFIG_FLASH_ADDR))
    {
        system_config_temp = *(system_config_t *)SYSTEM_CONFIG_FLASH_ADDR;
        return;
    }

    if (config_is_valid((system_config_t *)SYSTEM_CONFIG_LEGACY_FLASH_ADDR))
    {
        system_config_temp = *(system_config_t *)SYSTEM_CONFIG_LEGACY_FLASH_ADDR;
        config_save(&system_config_temp);
        return;
    }

    config_reset();
}
