#ifndef TASK_W5500
#define TASK_W5500
#include "stdint.h"
#include "socket.h"

typedef enum
{
    W5500_SOCKET_MODE_SERVER = 0,
    W5500_SOCKET_MODE_CLIENT
} w5500_socket_mode_t;
typedef struct
{
    uint8_t sn;
    uint8_t state;
    uint8_t ip[4];
    uint16_t port;
    w5500_socket_mode_t mode;
    uint8_t *buff;
} w5500_socket_t;

extern w5500_socket_t w5500_socket_mqtt;
extern w5500_socket_t w5500_socket_modbus;

void w5500_set_mac(uint8_t *mac);

void w5500_init();

void w5500_task();

void w5500_modbus_task();

void w5500_mqtt_client_task();

void w5500_dns(w5500_socket_t *w5500_socket, uint8_t *domain_name, uint8_t *domain_ip);

#endif
