#ifndef CONFIG_H
#define CONFIG_H
#include "stdint.h"

#define DEVICE_ID_LEN 32
#define MQTT_HOST_LEN 64
#define MQTT_USERNAME_LEN 32
#define MQTT_PASSWORD_LEN 32
#define MQTT_TOPIC_COUNT 5
#define MQTT_TOPIC_LEN 96
#define MQTT_NTP_SERVER_LEN 64
#define SYSTEM_CONFIG_MAGIC 0x474D3430UL
#define SYSTEM_CONFIG_VERSION 2UL

typedef struct
{
    uint8_t mode;
    uint8_t rev[3];
    uint8_t ip[4];
    uint8_t sn[4];
    uint8_t gw[4];
    uint8_t dns[4];
    uint8_t mac[6];
} eth_t;
typedef struct
{
    char host[64];
    uint16_t port;
    char username[32];
    char password[32];
    char topics[5][96];
    uint8_t qos[5];
    char ntp_server[64];
} mqtt_server_t;

typedef struct
{
    eth_t eth;
    char device_id[DEVICE_ID_LEN];
    mqtt_server_t mqtt_server;

    uint32_t magic;
    uint32_t version;
    uint32_t crc;
} system_config_t;
extern system_config_t *system_config;//只读模式系统配置，用于各种网络注册使用
extern system_config_t system_config_temp;//和上位机通信的临时配置

void config_save(system_config_t *system_config_in);

void config_check();
#endif
