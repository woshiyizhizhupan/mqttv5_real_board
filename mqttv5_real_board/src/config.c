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

#ifndef REAL_BOARD_DEFAULT_MQTT_HOST
#define REAL_BOARD_DEFAULT_MQTT_HOST "192.168.0.110"
#endif

#ifndef REAL_BOARD_DEFAULT_MQTT_PORT
#define REAL_BOARD_DEFAULT_MQTT_PORT 1883
#endif

#ifndef REAL_BOARD_DEFAULT_MQTT_USERNAME
#define REAL_BOARD_DEFAULT_MQTT_USERNAME ""
#endif

#ifndef REAL_BOARD_DEFAULT_MQTT_PASSWORD
#define REAL_BOARD_DEFAULT_MQTT_PASSWORD ""
#endif

#ifndef REAL_BOARD_DEFAULT_TLS_MODE
#define REAL_BOARD_DEFAULT_TLS_MODE 0
#endif

#ifndef REAL_BOARD_DEFAULT_TLS_VERIFY_PEER
#define REAL_BOARD_DEFAULT_TLS_VERIFY_PEER 1
#endif

#ifndef REAL_BOARD_DEFAULT_NTP_SERVER
#define REAL_BOARD_DEFAULT_NTP_SERVER "129.6.15.28"
#endif

#ifndef REAL_BOARD_DEFAULT_DEVICE_TYPE_NAME
#define REAL_BOARD_DEFAULT_DEVICE_TYPE_NAME "GM400"
#endif

#ifndef REAL_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX
#define REAL_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX "v1/devices/request/"
#endif

#ifndef REAL_BOARD_DEFAULT_RESPONSE_TOPIC_PREFIX
#define REAL_BOARD_DEFAULT_RESPONSE_TOPIC_PREFIX "v1/devices/response/"
#endif

system_config_t *system_config = (system_config_t *)SYSTEM_CONFIG_FLASH_ADDR;
system_config_t system_config_temp;

const system_config_t system_config_def = {
    .eth.mode = 1,
    .eth.ip = {192, 168, 0, 30},
    .eth.gw = {192, 168, 0, 1},
    .eth.sn = {255, 255, 255, 0},
    .eth.dns = {8, 8, 8, 8},
    .device_id = REAL_BOARD_DEFAULT_DEVICE_TYPE_NAME,

    .mqtt_server.host = REAL_BOARD_DEFAULT_MQTT_HOST,
    .mqtt_server.port = REAL_BOARD_DEFAULT_MQTT_PORT,
    .mqtt_server.username = REAL_BOARD_DEFAULT_MQTT_USERNAME,
    .mqtt_server.password = REAL_BOARD_DEFAULT_MQTT_PASSWORD,
    .mqtt_server.topics = {
        "v1/devices/response/{device_name}",
        "v1/devices/request/{device_name}",
        "v1/devices/response/{device_name}",
        "v1/devices/request/{device_name}",
        "v1/devices/response/{device_name}",
    },
    .mqtt_server.qos = {2, 2, 2, 2, 2},
    .mqtt_server.ntp_server = REAL_BOARD_DEFAULT_NTP_SERVER,
    .mqtt_server.tls_mode = REAL_BOARD_DEFAULT_TLS_MODE,
    .mqtt_server.tls_verify_peer = REAL_BOARD_DEFAULT_TLS_VERIFY_PEER};

static uint32_t config_calculate_crc(system_config_t *config)
{
    crc_reset();
    return crc_block_calculate((uint8_t *)config, offsetof(system_config_t, crc));
}

static uint8_t config_text_has_nul(const char *text, size_t len)
{
    return memchr(text, '\0', len) != NULL;
}

static uint8_t config_text_is_nonempty(const char *text, size_t len)
{
    return config_text_has_nul(text, len) && (text[0] != '\0');
}

static uint32_t config_ipv4_to_u32(const uint8_t ip[4])
{
    return ((uint32_t)ip[0] << 24) |
           ((uint32_t)ip[1] << 16) |
           ((uint32_t)ip[2] << 8) |
           (uint32_t)ip[3];
}

static uint8_t config_ipv4_is_all(const uint8_t ip[4], uint8_t value)
{
    return (ip[0] == value) && (ip[1] == value) && (ip[2] == value) && (ip[3] == value);
}

static uint8_t config_ipv4_is_usable_host(const uint8_t ip[4])
{
    if (config_ipv4_is_all(ip, 0U) || config_ipv4_is_all(ip, 255U))
        return 0;
    if ((ip[0] == 0U) || (ip[0] == 127U) || (ip[0] >= 224U))
        return 0;
    if ((ip[0] == 169U) && (ip[1] == 254U))
        return 0;
    return 1;
}

static uint8_t config_subnet_mask_is_valid(const uint8_t sn[4])
{
    uint32_t mask = config_ipv4_to_u32(sn);
    uint32_t inverted;

    if ((mask == 0U) || (mask == 0xFFFFFFFFUL))
        return 0;

    inverted = ~mask;
    return (inverted & (inverted + 1U)) == 0U;
}

static uint8_t config_ipv4_is_network_or_broadcast(const uint8_t ip[4], const uint8_t sn[4])
{
    uint32_t mask = config_ipv4_to_u32(sn);
    uint32_t host = config_ipv4_to_u32(ip) & ~mask;
    uint32_t host_mask = ~mask;

    return (host == 0U) || (host == host_mask);
}

static uint8_t config_same_subnet(const uint8_t left[4], const uint8_t right[4], const uint8_t sn[4])
{
    uint32_t mask = config_ipv4_to_u32(sn);
    return (config_ipv4_to_u32(left) & mask) == (config_ipv4_to_u32(right) & mask);
}

static uint8_t config_mac_is_valid(const uint8_t mac[6])
{
    uint8_t all_zero = 1U;
    uint8_t all_ff = 1U;

    for (uint8_t i = 0; i < 6U; i++)
    {
        if (mac[i] != 0U)
            all_zero = 0U;
        if (mac[i] != 0xFFU)
            all_ff = 0U;
    }

    if (all_zero || all_ff)
        return 0;
    if ((mac[0] & 0x01U) != 0U)
        return 0;
    return 1;
}

static uint8_t config_payload_is_valid(system_config_t *config)
{
    if (config == NULL)
        return 0;
    if (config->eth.mode > 1U)
        return 0;
    if (!config_mac_is_valid(config->eth.mac))
        return 0;

    if (config->eth.mode == 0U)
    {
        if (!config_ipv4_is_usable_host(config->eth.ip))
            return 0;
        if (!config_subnet_mask_is_valid(config->eth.sn))
            return 0;
        if (config_ipv4_is_network_or_broadcast(config->eth.ip, config->eth.sn))
            return 0;
        if (!config_ipv4_is_usable_host(config->eth.gw))
            return 0;
        if (!config_same_subnet(config->eth.ip, config->eth.gw, config->eth.sn))
            return 0;
        if (memcmp(config->eth.ip, config->eth.gw, 4U) == 0)
            return 0;
        if (!config_ipv4_is_usable_host(config->eth.dns))
            return 0;
    }

    if (!config_text_is_nonempty(config->device_id, sizeof(config->device_id)))
        return 0;
    if (!config_text_is_nonempty(config->mqtt_server.host, sizeof(config->mqtt_server.host)))
        return 0;
    if (config->mqtt_server.port == 0U)
        return 0;
    if (!config_text_has_nul(config->mqtt_server.username, sizeof(config->mqtt_server.username)))
        return 0;
    if (!config_text_has_nul(config->mqtt_server.password, sizeof(config->mqtt_server.password)))
        return 0;
    for (uint8_t i = 0; i < MQTT_TOPIC_COUNT; i++)
    {
        if (!config_text_is_nonempty(config->mqtt_server.topics[i], MQTT_TOPIC_LEN))
            return 0;
        if (config->mqtt_server.qos[i] > 2U)
            return 0;
    }
    if (!config_text_has_nul(config->mqtt_server.ntp_server, sizeof(config->mqtt_server.ntp_server)))
        return 0;
    if (config->mqtt_server.tls_mode > 2U)
        return 0;
    if (config->mqtt_server.tls_verify_peer > 1U)
        return 0;

    return 1;
}

static uint8_t config_is_valid(system_config_t *config)
{
    if (config->magic != SYSTEM_CONFIG_MAGIC)
        return 0;
    if (config->version != SYSTEM_CONFIG_VERSION)
        return 0;
    if (!config_payload_is_valid(config))
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

uint8_t config_save(system_config_t *system_config_in)
{
    if (!config_payload_is_valid(system_config_in))
        return 0;
    system_config_in->magic = SYSTEM_CONFIG_MAGIC;
    system_config_in->version = SYSTEM_CONFIG_VERSION;
    system_config_in->crc = config_calculate_crc(system_config_in);
    flash_erase_sector(SYSTEM_CONFIG_FLASH_ADDR);
    flash_write(SYSTEM_CONFIG_FLASH_ADDR, (uint8_t *)system_config_in, sizeof(system_config_t));
    return 1;
}

void config_reset()
{
    char request_topic[MQTT_TOPIC_LEN];
    char response_topic[MQTT_TOPIC_LEN];

    system_config_temp = system_config_def;
    get_mac(system_config_temp.eth.mac);
    snprintf(system_config_temp.device_id, sizeof(system_config_temp.device_id), "%s-%02X%02X%02X",
             REAL_BOARD_DEFAULT_DEVICE_TYPE_NAME,
             system_config_temp.eth.mac[3], system_config_temp.eth.mac[4], system_config_temp.eth.mac[5]);
    snprintf(request_topic, sizeof(request_topic), "%s%s", REAL_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX, system_config_temp.device_id);
    snprintf(response_topic, sizeof(response_topic), "%s%s", REAL_BOARD_DEFAULT_RESPONSE_TOPIC_PREFIX, system_config_temp.device_id);
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
