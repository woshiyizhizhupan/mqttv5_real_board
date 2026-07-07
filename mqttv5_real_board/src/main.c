#include "hc32f460.h"
#include "driver.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#include "task_w5500.h"
#include "config.h"
#include "real_board_business.h"
#include "real_board_ota.h"
#include "real_board_tls.h"
#include "real_board_ntp.h"
#include "real_board_lamp.h"
#include "real_board_hardware.h"

#define MQTT_USE_TLS 1
#define MQTT_TELEMETRY_TICKS 100U
#define TLS_HANDSHAKE_MAX_ATTEMPTS 5000U
#define REAL_BOARD_TLS_ERR_HANDSHAKE_TIMEOUT (-0x7103)
#define MQTT_STARTUP_DELAY_TICKS 100U
#define MQTT_RECONNECT_BACKOFF_TICKS 1000U

volatile uint32_t g_real_board_millis = 0U;

void sysclk_deinit()
{
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_MRC);
    /* Switch driver ability */
    PWC_HighPerformanceToHighSpeed();
    /* Set bus clk div. */
    CLK_SetClockDiv(CLK_BUS_CLK_ALL, (CLK_HCLK_DIV1 | CLK_EXCLK_DIV1 | CLK_PCLK0_DIV1 |
                                      CLK_PCLK1_DIV1 | CLK_PCLK2_DIV1 | CLK_PCLK3_DIV1 | CLK_PCLK4_DIV1));
    CLK_PLLCmd(DISABLE);
    CLK_XtalCmd(DISABLE);
    /* sram init include read/write wait cycle setting */
    SRAM_SetWaitCycle(SRAM_SRAM_ALL, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
    SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
    /* 0 cycles */
    GPIO_SetReadWaitCycle(GPIO_RD_WAIT0);
    /* flash read wait cycle setting */
    EFM_SetWaitCycle(EFM_WAIT_CYCLE0);
}

char *uint32_t_to_string(uint64_t value)
{
    static char buffer[17];
    sprintf(buffer, "%lX%lX", (uint32_t)(value >> 32), (uint32_t)value);
    return buffer;
}

void led_task()
{
    static uint32_t tick = 0;
    tick++;
    if (tick < 0xFFFF)
        return;
    tick = 0;
    led_toggle(0);
}

void trng_init()
{
    /* Enable TRNG. */
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_TRNG, ENABLE);
    /* TRNG initialization configuration. */
    TRNG_Init(TRNG_SHIFT_CNT64, TRNG_RELOAD_INIT_VAL_ENABLE);
    TRNG_Cmd(ENABLE);
}

static void timer0_1b_callback(void)
{
    TMR0_ClearStatus(CM_TMR0_1, TMR0_FLAG_CMP_B);

    static uint32_t count = 0;
    g_real_board_millis++;
    count++;
    if (count < 1000)
        return;
    count = 0;
    led_toggle(1);
    extern void DHCP_time_handler();
    DHCP_time_handler();
}

static void timer0_1b_init(void)
{
    stc_tmr0_init_t stcTmr0Init;
    stc_irq_signin_config_t stcIrqSignConfig;

    /* Enable timer0 and AOS clock */
    FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMR0_1, ENABLE);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_AOS, ENABLE);

    /* TIMER0 configuration */
    TMR0_StructInit(&stcTmr0Init);
    stcTmr0Init.u32ClockSrc = TMR0_CLK_SRC_INTERN_CLK;
    stcTmr0Init.u32ClockDiv = TMR0_CLK_DIV4;
    stcTmr0Init.u32Func = TMR0_FUNC_CMP;
    stcTmr0Init.u16CompareValue = 25 * 1000 - 1; // 1ms@100MHz
    TMR0_Init(CM_TMR0_1, TMR0_CH_B, &stcTmr0Init);
    TMR0_IntCmd(CM_TMR0_1, TMR0_INT_CMP_B, ENABLE);
    TMR0_Start(CM_TMR0_1, TMR0_CH_B);

    AOS_SetTriggerEventSrc(AOS_TMR0, EVT_SRC_TMR0_1_CMP_B);

    /* Interrupt configuration */
    stcIrqSignConfig.enIntSrc = INT_SRC_TMR0_1_CMP_B;
    stcIrqSignConfig.enIRQn = INT006_IRQn;
    stcIrqSignConfig.pfnCallback = &timer0_1b_callback;
    INTC_IrqSignIn(&stcIrqSignConfig);
    NVIC_ClearPendingIRQ(stcIrqSignConfig.enIRQn);
    NVIC_SetPriority(stcIrqSignConfig.enIRQn, DDL_IRQ_PRIO_DEFAULT);
    NVIC_EnableIRQ(stcIrqSignConfig.enIRQn);
}

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/debug.h"
#include "mbedtls/base64.h"

void modbus_task(void);
extern void w5500_tcp_server(w5500_socket_t *w5500_socket);

static void mqtt_service_management_channel(void)
{
    w5500_tcp_server(&w5500_socket_modbus);
    if (w5500_socket_modbus.state == SOCK_ESTABLISHED)
        modbus_task();
    led_task();
}

int socket_send(void *ctx, const unsigned char *buf, size_t len)
{
    int sock_fd = *(int *)ctx;
    int32_t ret = send(sock_fd, (uint8_t *)buf, len);
    switch (ret)
    {
    case SOCKERR_SOCKSTATUS:
        break;
    case SOCKERR_TIMEOUT:
        break;
    case SOCKERR_SOCKMODE:
        break;
    case SOCKERR_SOCKNUM:
        break;
    case SOCKERR_DATALEN:
        break;
    case SOCK_BUSY:
        break;
    default:
        break;
    }
    if (ret <= 0)
    {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return ret;
}

int socket_recv(void *ctx, unsigned char *buf, size_t len)
{
    int sock_fd = *(int *)ctx;
    uint16_t size;
    for (uint16_t timeout = 0;; timeout++)
    {
        size = getSn_RX_RSR(sock_fd); // Don't need to check SOCKERR_BUSY because it doesn't not occur.
        if (size > 0)
            break;
        if (timeout >= 50)
            return MBEDTLS_ERR_SSL_WANT_READ;
        mqtt_service_management_channel();
        delay_ms(1);
    }

    uint16_t read_len = size < len ? size : (uint16_t)len;
    int32_t ret = recv(sock_fd, (uint8_t *)buf, read_len);
    switch (ret)
    {
    case SOCKERR_SOCKSTATUS:
        break;
    case SOCKERR_SOCKMODE:
        break;
    case SOCKERR_SOCKNUM:
        break;
    case SOCKERR_DATALEN:
        break;
    case SOCK_BUSY:
        break;
    default:
        break;
    }
    if (ret <= 0)
    {
        // 根据你的 W5500 send 返回值映射错误
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return ret;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    uint32_t rnd;
    int32_t ret;
    (void)data; // 避免未使用参数警告

    size_t output_len = 0;

    // 确保输出缓冲区长度至少为 len
    if (output == NULL || olen == NULL)
    {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    // 生成随机数，每次获取 4 字节
    for (size_t i = 0; i < len; i += 4)
    {
        // 获取 32 位随机数

        ret = TRNG_GenerateRandom(&rnd, 1);
        if (ret != LL_OK)
        {
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED; // 获取随机数失败
        }

        // 计算本次要复制的字节数（最后可能不足4字节）
        size_t copy_len = (len - i >= 4) ? 4 : (len - i);

        // 复制到输出缓冲区
        memcpy(output + i, &rnd, copy_len);

        output_len += copy_len;
    }

    // 设置实际输出的字节数
    *olen = output_len;

    // 返回 0 表示成功
    return 0;
}

mbedtls_ssl_context ssl;
mbedtls_ssl_config conf;
mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context ctr_drbg;
mbedtls_x509_crt ca_cert;
mbedtls_x509_crt client_cert;
mbedtls_pk_context client_key;
static uint8_t mqtt_tls_active = 0;
static uint8_t mqtt_tls_context_initialized = 0;
volatile int32_t mqtt_last_tls_error = 0;
volatile uint8_t mqtt_last_tls_stage = 0;

void ssl_printf(void *ctx, int level, const char *file, int line, const char *str)
{
    printf("[%d] %s:%d: %s", level, file, line, str);
}

static void mqtt_tls_cleanup(void)
{
#if MQTT_USE_TLS
    if (mqtt_tls_context_initialized)
    {
        mbedtls_pk_free(&client_key);
        mbedtls_x509_crt_free(&client_cert);
        mbedtls_x509_crt_free(&ca_cert);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ssl_free(&ssl);
        mqtt_tls_context_initialized = 0;
    }
    mqtt_tls_active = 0;
#endif
}

int tls_handshake(uint8_t *socket_fd)
{
    int ret;
    uint32_t attempts = 0;

    mqtt_last_tls_error = 0;
    mqtt_last_tls_stage = 1;

    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);
    mbedtls_x509_crt_init(&ca_cert);
    mbedtls_x509_crt_init(&client_cert);
    mbedtls_pk_init(&client_key);
    mqtt_tls_context_initialized = 1;

    // mbedtls_ssl_conf_dbg(&conf, ssl_printf, stdout);
    // mbedtls_debug_set_threshold(1);

    // 初始化配置
    if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
    {
        printf("mbedtls_ssl_config_defaults failed: -0x%04x\n", -ret);
        mqtt_last_tls_error = ret;
        return ret;
    }

    // 配置随机数
    mqtt_last_tls_stage = 2;
    ret = mbedtls_entropy_add_source(&entropy, mbedtls_hardware_poll, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (ret != 0)
    {
        printf("mbedtls_entropy_add_source failed: -0x%04x\n", -ret);
        mqtt_last_tls_error = ret;
        return ret;
    }
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0)) != 0)
    {
        printf("mbedtls_ctr_drbg_seed failed: -0x%04x\n", -ret);
        mqtt_last_tls_error = ret;
        return ret;
    }

    mqtt_last_tls_stage = 3;
    ret = real_board_tls_setup(system_config, &ssl, &conf, &ca_cert, &client_cert, &client_key, &ctr_drbg);
    if (ret != 0)
    {
        printf("real_board_tls_setup failed: -0x%04x\n", -ret);
        mqtt_last_tls_error = ret;
        return ret;
    }

    // 设置IO
    mbedtls_ssl_set_bio(&ssl, socket_fd, socket_send, socket_recv, NULL);

    // 应用配置
    mqtt_last_tls_stage = 4;
    if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0)
    {
        printf("mbedtls_ssl_setup failed: -0x%04x\n", -ret);
        mqtt_last_tls_error = ret;
        return ret;
    }

    // 握手
    mqtt_last_tls_stage = 5;
    printf("Starting SSL handshake...\n");
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0)
    {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            printf("Handshake failed: -0x%04x\n", -ret);
            mqtt_last_tls_error = ret;
            return ret;
        }
        mqtt_service_management_channel();
        attempts++;
        if (attempts >= TLS_HANDSHAKE_MAX_ATTEMPTS)
        {
            printf("Handshake timeout after %lu attempts\n", (unsigned long)attempts);
            mqtt_last_tls_error = REAL_BOARD_TLS_ERR_HANDSHAKE_TIMEOUT;
            return REAL_BOARD_TLS_ERR_HANDSHAKE_TIMEOUT;
        }
        delay_ms(1);
    }

    mqtt_last_tls_stage = 6;
    printf("TLS handshake successful!\n");

    return 0;
}

#include "MQTTV5Packet.h"
#include "cJSON.h"
#include "v5log.h"

#define MQTT_PACKET_BUF_SIZE 5120
#define MQTT_JSON_BUF_SIZE 4096
#define MQTT_LEGACY_RESPONSE_FRM_TYPE 0xA5U
#define MQTT_LEGACY_SYNC_RESPONSE_CMD 0x81U
#define MQTT_SERVER_REQUEST_TOPIC_PREFIX "v1/devices/request/"
#define MQTT_SERVER_RESPONSE_TOPIC_PREFIX "v1/devices/response/"
#define MQTT_SERVER_STATUS_TOPIC_PREFIX "v1/devices/status/"

typedef enum
{
    MQTT_TOPIC_TELEMETRY_UP_INDEX = 0,
    MQTT_TOPIC_CMD_DOWN_INDEX = 1,
    MQTT_TOPIC_EVENT_UP_INDEX = 2,
    MQTT_TOPIC_OTA_INDEX = 3,
    MQTT_TOPIC_DEBUG_UP_INDEX = 4,
} mqtt_topic_index_t;

typedef enum
{
    MQTT_STATE_INIT,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_DISCONNECTED,
} mqtt_state_t;
static mqtt_state_t state = MQTT_STATE_INIT;
static uint16_t mqtt_reconnect_backoff_ticks = MQTT_STARTUP_DELAY_TICKS;
static char mqtt_runtime_response_topic[MQTT_TOPIC_LEN];
static char mqtt_runtime_request_topic[MQTT_TOPIC_LEN];
static char mqtt_runtime_status_topic[MQTT_TOPIC_LEN];
static char mqtt_runtime_status_payload[192];

static void mqtt_reset_connection(void)
{
#if MQTT_USE_TLS
    mqtt_tls_cleanup();
#endif
    close(w5500_socket_mqtt.sn);
    w5500_socket_mqtt.state = SOCK_CLOSED;
    state = MQTT_STATE_INIT;
    mqtt_reconnect_backoff_ticks = MQTT_RECONNECT_BACKOFF_TICKS;
}

static uint8_t mqtt_backoff_task(void)
{
    if (mqtt_reconnect_backoff_ticks == 0)
        return 0;

    mqtt_reconnect_backoff_ticks--;
    if (getSn_SR(w5500_socket_mqtt.sn) != SOCK_CLOSED)
    {
        close(w5500_socket_mqtt.sn);
        w5500_socket_mqtt.state = SOCK_CLOSED;
    }
    mqtt_service_management_channel();
    delay_ms(1);
    return 1;
}

int mqtt_send(uint8_t *data, int len)
{
    int ret;
#if MQTT_USE_TLS
    if (mqtt_tls_active)
    {
        ret = mbedtls_ssl_write(&ssl, data, len);
        return ret;
    }
#endif
    ret = send(w5500_socket_mqtt.sn, data, len);
    // // 关键：将WANT_READ转换为MQTT库能理解的"无数据"状态
    // if (ret == MBEDTLS_ERR_SSL_WANT_WRITE)
    // {
    //     return 0; // 返回0表示没有数据，不是错误
    // }
    return ret;
}
int mqtt_recv(uint8_t *data, int len)
{
    int ret;
#if MQTT_USE_TLS
    if (mqtt_tls_active)
    {
        ret = mbedtls_ssl_read(&ssl, data, len);
        // 关键：将WANT_READ转换为MQTT库能理解的"无数据"状态
        if (ret == MBEDTLS_ERR_SSL_WANT_READ)
        {
            return 0; // 返回0表示没有数据，不是错误
        }
        return ret;
    }
#endif
    uint16_t size;
    for (uint16_t timeout = 0;; timeout++)
    {
        size = getSn_RX_RSR(w5500_socket_mqtt.sn);
        if (size > 0)
        {
            break;
        }
        delay_ms(1);
        if (timeout >= 50)
        {
            return 0;
        }
    }
    ret = recv(w5500_socket_mqtt.sn, data, len);
    if (ret <= 0)
    {
        return 0;
    }
    return ret;
}

uint8_t mqtt_init()
{
#if MQTT_USE_TLS
    mqtt_tls_cleanup();
    if (!real_board_tls_is_enabled(system_config))
    {
        printf("MQTT plain TCP mode enabled\r\n");
        return 0;
    }
    // ssl握手
    int ret;
    ret = tls_handshake(&w5500_socket_mqtt.sn);
    if (ret != 0)
    {
        printf("SSL initialization failed: %d\n", ret);
        mqtt_reset_connection();
        return 1;
    }
    mqtt_tls_active = 1;
#else
    printf("MQTT plain TCP mode enabled\r\n");
#endif
    return 0;
}

unsigned short msgid = 1;
unsigned char mqtt_buff[MQTT_PACKET_BUF_SIZE];
int mqtt_buff_len = sizeof(mqtt_buff);
static char mqtt_json_buf[MQTT_JSON_BUF_SIZE];
static char mqtt_payload_json_decode_buf[MQTT_JSON_BUF_SIZE];
static uint8_t mqtt_reboot_pending = 0;

MQTTString topicString = MQTTString_initializer;
MQTTProperties recv_properties = MQTTProperties_initializer;
MQTTProperties sub_properties = MQTTProperties_initializer;
MQTTSubscribe_options sub_options = {0};

MQTTProperty send_properties_array[2];
MQTTProperties send_properties = MQTTProperties_initializer;

unsigned char dup;
unsigned char qos;
unsigned char retained;

MQTTString receivedTopic;

static const char *mqtt_get_device_name(void)
{
    return system_config->device_id[0] != '\0' ? system_config->device_id : "GM400";
}

static const char *mqtt_get_request_topic(void)
{
    if (system_config->mqtt_server.topics[MQTT_TOPIC_CMD_DOWN_INDEX][0] != '\0')
        return system_config->mqtt_server.topics[MQTT_TOPIC_CMD_DOWN_INDEX];

    snprintf(mqtt_runtime_request_topic,
             sizeof(mqtt_runtime_request_topic),
             "%s%s",
             MQTT_SERVER_REQUEST_TOPIC_PREFIX,
             mqtt_get_device_name());
    return mqtt_runtime_request_topic;
}

static const char *mqtt_get_response_topic(void)
{
    if (system_config->mqtt_server.topics[MQTT_TOPIC_TELEMETRY_UP_INDEX][0] != '\0')
        return system_config->mqtt_server.topics[MQTT_TOPIC_TELEMETRY_UP_INDEX];

    snprintf(mqtt_runtime_response_topic,
             sizeof(mqtt_runtime_response_topic),
             "%s%s",
             MQTT_SERVER_RESPONSE_TOPIC_PREFIX,
             mqtt_get_device_name());
    return mqtt_runtime_response_topic;
}

static const char *mqtt_get_status_topic(void)
{
    snprintf(mqtt_runtime_status_topic,
             sizeof(mqtt_runtime_status_topic),
             "%s%s",
             MQTT_SERVER_STATUS_TOPIC_PREFIX,
             mqtt_get_device_name());
    return mqtt_runtime_status_topic;
}

static char *mqtt_make_status_payload(const char *status, const char *reason)
{
    snprintf(mqtt_runtime_status_payload,
             sizeof(mqtt_runtime_status_payload),
             "{\"schema\":\"emqx-gateway.status.v1\",\"device_id\":\"%s\",\"status\":\"%s\",\"reason\":\"%s\"}",
             mqtt_get_device_name(),
             status,
             reason);
    return mqtt_runtime_status_payload;
}

static int mqtt_publish_json_to_topic_ex(const char *topic, const char *json, uint8_t qos_level, uint8_t retain_flag)
{
    int len;
    uint16_t publish_msgid = qos_level ? msgid++ : 0;

    topicString.cstring = (char *)topic;
    len = MQTTV5Serialize_publish(mqtt_buff, mqtt_buff_len, 0, qos_level, retain_flag, publish_msgid, topicString,
                                  &send_properties, (unsigned char *)json, strlen(json));
    if (len <= 0)
        return len;
    return mqtt_send(mqtt_buff, len);
}

static int mqtt_publish_json_to_topic(const char *topic, const char *json, uint8_t qos_level)
{
    return mqtt_publish_json_to_topic_ex(topic, json, qos_level, 0U);
}

static int mqtt_publish_response_json(const char *json, uint8_t qos_level)
{
    return mqtt_publish_json_to_topic(mqtt_get_response_topic(), json, qos_level);
}

static uint8_t mqtt_received_topic_matches(mqtt_topic_index_t topic_index)
{
    const char *expected = system_config->mqtt_server.topics[topic_index];
    size_t expected_len = strlen(expected);

    if ((receivedTopic.lenstring.data != NULL) && (receivedTopic.lenstring.len > 0))
    {
        return ((size_t)receivedTopic.lenstring.len == expected_len) &&
               (memcmp(receivedTopic.lenstring.data, expected, expected_len) == 0);
    }
    if (receivedTopic.cstring != NULL)
        return strcmp(receivedTopic.cstring, expected) == 0;
    return 0;
}

static void mqtt_add_config_json(cJSON *object, system_config_t *config)
{
    cJSON *topics;
    cJSON *qos_values;

    cJSON_AddStringToObject(object, "device_id", config->device_id);
    cJSON_AddStringToObject(object, "host", config->mqtt_server.host);
    cJSON_AddNumberToObject(object, "port", config->mqtt_server.port);
    cJSON_AddStringToObject(object, "username", config->mqtt_server.username);
    cJSON_AddBoolToObject(object, "password_set", config->mqtt_server.password[0] != '\0');
    cJSON_AddStringToObject(object, "ntp_server", config->mqtt_server.ntp_server);
    cJSON_AddNumberToObject(object, "tls_mode", config->mqtt_server.tls_mode);
    cJSON_AddBoolToObject(object, "tls_verify_peer", config->mqtt_server.tls_verify_peer);

    topics = cJSON_AddArrayToObject(object, "topics");
    qos_values = cJSON_AddArrayToObject(object, "qos");
    for (uint8_t i = 0; i < MQTT_TOPIC_COUNT; i++)
    {
        cJSON_AddItemToArray(topics, cJSON_CreateString(config->mqtt_server.topics[i]));
        cJSON_AddItemToArray(qos_values, cJSON_CreateNumber(config->mqtt_server.qos[i]));
    }
}

static void mqtt_publish_response(const char *id, const char *cmd, uint8_t ok, int code, const char *message, cJSON *data)
{
    cJSON *root = cJSON_CreateObject();
    int response_code = code;
    if (root == NULL)
    {
        if (data != NULL)
            cJSON_Delete(data);
        return;
    }

    if (ok && data != NULL)
    {
        cJSON *legacy = cJSON_GetObjectItemCaseSensitive(data, "legacy_protocol");
        if (cJSON_IsTrue(legacy))
            response_code = 200;
    }

    cJSON_AddStringToObject(root, "schema", "emqx-gateway.response.v1");
    if ((id != NULL) && (id[0] != '\0'))
        cJSON_AddStringToObject(root, "id", id);
    cJSON_AddStringToObject(root, "device_id", system_config->device_id);
    cJSON_AddStringToObject(root, "cmd", (cmd != NULL) ? cmd : "");
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddNumberToObject(root, "code", response_code);
    cJSON_AddStringToObject(root, "message", (message != NULL) ? message : "");
    if (data != NULL)
        cJSON_AddItemToObject(root, "data", data);

    if (cJSON_PrintPreallocated(root, mqtt_json_buf, sizeof(mqtt_json_buf), 0))
        mqtt_publish_response_json(mqtt_json_buf, system_config->mqtt_server.qos[MQTT_TOPIC_EVENT_UP_INDEX]);
    cJSON_Delete(root);
}

static uint8_t mqtt_is_legacy_payload_envelope(cJSON *root)
{
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsString(payload) || payload->valuestring == NULL)
        return 0;

    return 1;
}

static int mqtt_hex_nibble(char ch)
{
    if ((ch >= '0') && (ch <= '9'))
        return ch - '0';
    if ((ch >= 'a') && (ch <= 'f'))
        return ch - 'a' + 10;
    if ((ch >= 'A') && (ch <= 'F'))
        return ch - 'A' + 10;
    return -1;
}

static uint8_t mqtt_decode_payload_hex_prefix(const char *text, uint8_t *prefix, uint8_t prefix_len)
{
    uint8_t count = 0;
    uint8_t high = 0;
    uint8_t has_high = 0;

    if ((text == NULL) || (prefix == NULL))
        return 0;

    while ((*text != '\0') && (count < prefix_len))
    {
        int nibble;
        if ((*text == ' ') || (*text == '\r') || (*text == '\n') || (*text == '\t'))
        {
            text++;
            continue;
        }

        nibble = mqtt_hex_nibble(*text++);
        if (nibble < 0)
            return 0;

        if (!has_high)
        {
            high = (uint8_t)nibble;
            has_high = 1;
        }
        else
        {
            prefix[count++] = (uint8_t)((high << 4) | (uint8_t)nibble);
            has_high = 0;
        }
    }

    return (count == prefix_len) ? 1U : 0U;
}

static uint8_t mqtt_is_legacy_response_payload_envelope(cJSON *root)
{
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    uint8_t prefix[3];

    if (!cJSON_IsString(payload) || payload->valuestring == NULL)
        return 0;

    if (!mqtt_decode_payload_hex_prefix(payload->valuestring, prefix, sizeof(prefix)))
        return 0;

    return (prefix[0] == REAL_BOARD_FRM_PREAMBLE) &&
           (prefix[1] == MQTT_LEGACY_RESPONSE_FRM_TYPE) &&
           (prefix[2] == MQTT_LEGACY_SYNC_RESPONSE_CMD);
}

static uint8_t mqtt_is_board_publish_schema(cJSON *root)
{
    cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const char *value;

    if (!cJSON_IsString(schema) || schema->valuestring == NULL)
        return 0;

    value = schema->valuestring;
    return (strcmp(value, "emqx-gateway.realboard.telemetry.v1") == 0) ||
           (strcmp(value, "emqx-gateway.realboard.event.v1") == 0) ||
           (strcmp(value, "emqx-gateway.debug.v1") == 0) ||
           (strcmp(value, "emqx-gateway.status.v1") == 0) ||
           (strcmp(value, "emqx-gateway.response.v1") == 0);
}

static void mqtt_add_envelope_string_or_default(cJSON *root, cJSON *request, const char *name, const char *default_value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(request, name);
    if (cJSON_IsString(item) && item->valuestring != NULL)
        cJSON_AddStringToObject(root, name, item->valuestring);
    else
        cJSON_AddStringToObject(root, name, default_value);
}

static void mqtt_add_envelope_item_if_present(cJSON *root, cJSON *request, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(request, name);
    cJSON *copy;

    if (item == NULL)
        return;

    copy = cJSON_Duplicate(item, 1);
    if (copy != NULL)
        cJSON_AddItemToObject(root, name, copy);
}

static void mqtt_publish_legacy_payload_envelope_response(cJSON *request,
                                                          cJSON *legacy_data,
                                                          uint8_t ok,
                                                          int code,
                                                          const char *message,
                                                          uint8_t received_on_ota_topic)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *payload_hex = NULL;
    const char *payload_text = "";
    (void)ok;
    (void)code;
    (void)message;
    (void)received_on_ota_topic;

    if (legacy_data != NULL)
        payload_hex = cJSON_GetObjectItemCaseSensitive(legacy_data, "frame_hex");
    if (cJSON_IsString(payload_hex) && payload_hex->valuestring != NULL)
        payload_text = payload_hex->valuestring;

    if (root == NULL)
    {
        if (legacy_data != NULL)
            cJSON_Delete(legacy_data);
        return;
    }

    mqtt_add_envelope_item_if_present(root, request, "id");
    mqtt_add_envelope_item_if_present(root, request, "mutable");
    mqtt_add_envelope_item_if_present(root, request, "qos");
    mqtt_add_envelope_item_if_present(root, request, "retained");
    mqtt_add_envelope_item_if_present(root, request, "dup");
    mqtt_add_envelope_item_if_present(root, request, "messageId");
    mqtt_add_envelope_item_if_present(root, request, "properties");
    mqtt_add_envelope_string_or_default(root, request, "connectType", "1");
    mqtt_add_envelope_string_or_default(root, request, "msgType", "1");
    cJSON_AddStringToObject(root, "payload", payload_text);
    mqtt_add_envelope_string_or_default(root, request, "timestamp", "0");

    if (cJSON_PrintPreallocated(root, mqtt_json_buf, sizeof(mqtt_json_buf), 0))
        mqtt_publish_response_json(mqtt_json_buf, system_config->mqtt_server.qos[MQTT_TOPIC_EVENT_UP_INDEX]);

    cJSON_Delete(root);
    if (legacy_data != NULL)
        cJSON_Delete(legacy_data);
}

static void mqtt_publish_status(const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
        return;

    cJSON_AddStringToObject(root, "schema", "emqx-gateway.status.v1");
    cJSON_AddStringToObject(root, "device_id", system_config->device_id);
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "message", message);

    if (cJSON_PrintPreallocated(root, mqtt_json_buf, sizeof(mqtt_json_buf), 0))
        mqtt_publish_response_json(mqtt_json_buf, system_config->mqtt_server.qos[MQTT_TOPIC_EVENT_UP_INDEX]);
    cJSON_Delete(root);
}

static void mqtt_publish_lifecycle_status(const char *status, const char *reason)
{
    const char *payload = mqtt_make_status_payload(status, reason);
    mqtt_publish_json_to_topic_ex(mqtt_get_status_topic(), payload, 0U, 1U);
}

static void mqtt_publish_debug(const char *event, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
        return;

    cJSON_AddStringToObject(root, "schema", "emqx-gateway.debug.v1");
    cJSON_AddStringToObject(root, "device_id", system_config->device_id);
    cJSON_AddStringToObject(root, "event", event);
    cJSON_AddStringToObject(root, "message", message);

    if (cJSON_PrintPreallocated(root, mqtt_json_buf, sizeof(mqtt_json_buf), 0))
        mqtt_publish_response_json(mqtt_json_buf, system_config->mqtt_server.qos[MQTT_TOPIC_DEBUG_UP_INDEX]);
    cJSON_Delete(root);
}

static void mqtt_publish_telemetry(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
        return;

    real_board_business_append_telemetry(root);

    if (cJSON_PrintPreallocated(root, mqtt_json_buf, sizeof(mqtt_json_buf), 0))
        mqtt_publish_response_json(mqtt_json_buf, system_config->mqtt_server.qos[MQTT_TOPIC_TELEMETRY_UP_INDEX]);
    cJSON_Delete(root);
}

static void mqtt_publish_real_event(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
        return;

    if (real_board_business_publish_event(root) &&
        cJSON_PrintPreallocated(root, mqtt_json_buf, sizeof(mqtt_json_buf), 0))
    {
        mqtt_publish_response_json(mqtt_json_buf, system_config->mqtt_server.qos[MQTT_TOPIC_EVENT_UP_INDEX]);
    }

    cJSON_Delete(root);
}

static void mqtt_get_json_id(cJSON *root, char *id, size_t id_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "id");
    id[0] = '\0';
    if (item == NULL)
        item = cJSON_GetObjectItemCaseSensitive(root, "messageId");
    if (item == NULL)
        item = cJSON_GetObjectItemCaseSensitive(root, "message_id");
    if (item == NULL)
        return;
    if (cJSON_IsString(item) && (item->valuestring != NULL))
    {
        snprintf(id, id_len, "%s", item->valuestring);
    }
    else if (cJSON_IsNumber(item))
    {
        snprintf(id, id_len, "%d", item->valueint);
    }
}

static cJSON *mqtt_try_parse_payload_json_text(const char *payload_text)
{
    const char *trimmed = payload_text;
    if (payload_text == NULL)
        return NULL;

    while ((*trimmed == ' ') || (*trimmed == '\r') || (*trimmed == '\n') || (*trimmed == '\t'))
        trimmed++;
    if (*trimmed != '{')
        return NULL;

    return cJSON_ParseWithLength(payload_text, strlen(payload_text));
}

static cJSON *mqtt_try_parse_payload_base64_json(const char *payload_text)
{
    size_t decoded_len = 0;
    char *trimmed;

    if ((payload_text == NULL) || (payload_text[0] == '\0'))
        return NULL;

    if (mbedtls_base64_decode((unsigned char *)mqtt_payload_json_decode_buf,
                              sizeof(mqtt_payload_json_decode_buf) - 1U,
                              &decoded_len,
                              (const unsigned char *)payload_text,
                              strlen(payload_text)) != 0)
    {
        return NULL;
    }

    mqtt_payload_json_decode_buf[decoded_len] = '\0';
    trimmed = mqtt_payload_json_decode_buf;
    while ((*trimmed == ' ') || (*trimmed == '\r') || (*trimmed == '\n') || (*trimmed == '\t'))
        trimmed++;
    if (*trimmed != '{')
        return NULL;

    return cJSON_ParseWithLength(mqtt_payload_json_decode_buf, decoded_len);
}

static cJSON *mqtt_unwrap_local_payload_json(cJSON *root)
{
    cJSON *payload_item;
    cJSON *inner_json;

    if (!cJSON_IsObject(root))
        return NULL;

    payload_item = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsString(payload_item) || (payload_item->valuestring == NULL))
        return NULL;

    inner_json = mqtt_try_parse_payload_json_text(payload_item->valuestring);
    if (inner_json != NULL)
        return inner_json;

    return mqtt_try_parse_payload_base64_json(payload_item->valuestring);
}

static void mqtt_delete_command_roots(cJSON *root, cJSON *command_root)
{
    if ((command_root != NULL) && (command_root != root))
        cJSON_Delete(command_root);
    cJSON_Delete(root);
}

static uint8_t mqtt_copy_json_string(cJSON *params, const char *name, char *dest, size_t dest_len, char *error, size_t error_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(params, name);
    if (item == NULL)
        return 1;
    if (!cJSON_IsString(item) || (item->valuestring == NULL))
    {
        snprintf(error, error_len, "%s must be a string", name);
        return 0;
    }
    if (strlen(item->valuestring) >= dest_len)
    {
        snprintf(error, error_len, "%s is too long", name);
        return 0;
    }
    memset(dest, 0, dest_len);
    strcpy(dest, item->valuestring);
    return 1;
}

static uint8_t mqtt_apply_config_json(cJSON *params, uint8_t *reboot_requested, char *error, size_t error_len)
{
    cJSON *item;
    system_config_t next_config = *system_config;

    if (!cJSON_IsObject(params))
    {
        snprintf(error, error_len, "params must be an object");
        return 0;
    }

    if (!mqtt_copy_json_string(params, "device_id", next_config.device_id, sizeof(next_config.device_id), error, error_len))
        return 0;
    if (!mqtt_copy_json_string(params, "host", next_config.mqtt_server.host, sizeof(next_config.mqtt_server.host), error, error_len))
        return 0;
    if (!mqtt_copy_json_string(params, "username", next_config.mqtt_server.username, sizeof(next_config.mqtt_server.username), error, error_len))
        return 0;
    if (!mqtt_copy_json_string(params, "password", next_config.mqtt_server.password, sizeof(next_config.mqtt_server.password), error, error_len))
        return 0;
    if (!mqtt_copy_json_string(params, "ntp_server", next_config.mqtt_server.ntp_server, sizeof(next_config.mqtt_server.ntp_server), error, error_len))
        return 0;

    item = cJSON_GetObjectItemCaseSensitive(params, "port");
    if (item != NULL)
    {
        if (!cJSON_IsNumber(item) || (item->valueint <= 0) || (item->valueint > 65535))
        {
            snprintf(error, error_len, "port must be 1-65535");
            return 0;
        }
        next_config.mqtt_server.port = (uint16_t)item->valueint;
    }

    item = cJSON_GetObjectItemCaseSensitive(params, "tls_mode");
    if (item != NULL)
    {
        if (!cJSON_IsNumber(item) || item->valueint < 0 || item->valueint > 2)
        {
            snprintf(error, error_len, "tls_mode must be 0, 1 or 2");
            return 0;
        }
        next_config.mqtt_server.tls_mode = (uint8_t)item->valueint;
    }

    item = cJSON_GetObjectItemCaseSensitive(params, "tls_verify_peer");
    if (item != NULL)
        next_config.mqtt_server.tls_verify_peer = cJSON_IsFalse(item) ? 0U : 1U;

    item = cJSON_GetObjectItemCaseSensitive(params, "topics");
    if (item != NULL)
    {
        if (!cJSON_IsArray(item) || (cJSON_GetArraySize(item) != MQTT_TOPIC_COUNT))
        {
            snprintf(error, error_len, "topics must contain five strings");
            return 0;
        }
        for (uint8_t i = 0; i < MQTT_TOPIC_COUNT; i++)
        {
            cJSON *topic = cJSON_GetArrayItem(item, i);
            if (!cJSON_IsString(topic) || (topic->valuestring == NULL) || (strlen(topic->valuestring) >= MQTT_TOPIC_LEN))
            {
                snprintf(error, error_len, "topic %d is invalid", i + 1);
                return 0;
            }
            memset(next_config.mqtt_server.topics[i], 0, MQTT_TOPIC_LEN);
            strcpy(next_config.mqtt_server.topics[i], topic->valuestring);
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(params, "qos");
    if (item != NULL)
    {
        if (!cJSON_IsArray(item) || (cJSON_GetArraySize(item) != MQTT_TOPIC_COUNT))
        {
            snprintf(error, error_len, "qos must contain five numbers");
            return 0;
        }
        for (uint8_t i = 0; i < MQTT_TOPIC_COUNT; i++)
        {
            cJSON *qos_item = cJSON_GetArrayItem(item, i);
            if (!cJSON_IsNumber(qos_item) || (qos_item->valueint < 0) || (qos_item->valueint > 2))
            {
                snprintf(error, error_len, "qos %d must be 0, 1 or 2", i + 1);
                return 0;
            }
            next_config.mqtt_server.qos[i] = (uint8_t)qos_item->valueint;
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(params, "reboot");
    *reboot_requested = cJSON_IsTrue(item) ? 1 : 0;

    system_config_temp = next_config;
    config_save(&system_config_temp);
    return 1;
}

static void mqtt_handle_command_json(uint8_t *payload_in, int32_t payloadlen_in, uint8_t received_on_ota_topic)
{
    char id[40];
    char error[80];
    cJSON *root;
    cJSON *command_root;
    cJSON *unwrapped_root;
    cJSON *cmd_item;
    uint8_t legacy_payload_envelope;
    const char *cmd;
#if REAL_BOARD_ENABLE_LEGACY_FRAME
    cJSON *legacy_data;
    int legacy_code;
    const char *legacy_message;
    uint8_t legacy_reboot_requested;
#endif
    cJSON *module_data;
    int module_code;
    const char *module_message;
    uint8_t module_reboot_requested;

    root = cJSON_ParseWithLength((const char *)payload_in, (size_t)payloadlen_in);
    if (root == NULL)
    {
        mqtt_publish_debug("invalid_json", "cmd/down payload is not valid JSON");
        mqtt_publish_response(NULL, NULL, 0, 400, "invalid JSON", NULL);
        return;
    }

    command_root = root;
    unwrapped_root = mqtt_unwrap_local_payload_json(root);
    if (unwrapped_root != NULL)
        command_root = unwrapped_root;

    mqtt_get_json_id(command_root, id, sizeof(id));
    if ((id[0] == '\0') && (command_root != root))
        mqtt_get_json_id(root, id, sizeof(id));
    legacy_payload_envelope = mqtt_is_legacy_payload_envelope(command_root);

    if (mqtt_is_board_publish_schema(command_root))
    {
        mqtt_delete_command_roots(root, command_root);
        return;
    }

    if (legacy_payload_envelope && mqtt_is_legacy_response_payload_envelope(command_root))
    {
        mqtt_delete_command_roots(root, command_root);
        return;
    }

    cmd_item = cJSON_GetObjectItemCaseSensitive(command_root, "cmd");
    if (!cJSON_IsString(cmd_item) || (cmd_item->valuestring == NULL))
    {
#if REAL_BOARD_ENABLE_LEGACY_FRAME
        if (real_board_business_handle_legacy_command(command_root, "legacy_frame", &legacy_data, &legacy_code, &legacy_message, &legacy_reboot_requested))
        {
            if (legacy_payload_envelope)
                mqtt_publish_legacy_payload_envelope_response(command_root,
                                                              legacy_data,
                                                              legacy_code == 0,
                                                              legacy_code,
                                                              legacy_message,
                                                              received_on_ota_topic);
            else
                mqtt_publish_response(id, "legacy_frame", legacy_code == 0, legacy_code, legacy_message, legacy_data);
            if (legacy_reboot_requested)
                mqtt_reboot_pending = 1;
        }
        else
        {
            mqtt_publish_response(id, NULL, 0, 400, "cmd must be a string", NULL);
        }
#else
        mqtt_publish_response(id, NULL, 0, 400, "cmd must be a string", NULL);
#endif
        mqtt_delete_command_roots(root, command_root);
        return;
    }
    cmd = cmd_item->valuestring;

    if (strcmp(cmd, "ping") == 0)
    {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddBoolToObject(data, "pong", 1);
        mqtt_publish_response(id, cmd, 1, 0, "ok", data);
    }
    else if (strcmp(cmd, "get_status") == 0)
    {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "host", system_config->mqtt_server.host);
        cJSON_AddNumberToObject(data, "port", system_config->mqtt_server.port);
        cJSON_AddStringToObject(data, "mqtt_state", "connected");
        real_board_business_append_status(data);
        real_board_ota_append_status(data);
        real_board_ntp_append_status(data);
        mqtt_publish_response(id, cmd, 1, 0, "ok", data);
    }
    else if (strcmp(cmd, "get_config") == 0)
    {
        cJSON *data = cJSON_CreateObject();
        mqtt_add_config_json(data, system_config);
        mqtt_publish_response(id, cmd, 1, 0, "ok", data);
    }
    else if (strcmp(cmd, "set_config") == 0)
    {
        uint8_t reboot_requested = 0;
        cJSON *params = cJSON_GetObjectItemCaseSensitive(command_root, "params");
        if (mqtt_apply_config_json(params, &reboot_requested, error, sizeof(error)))
        {
            cJSON *data = cJSON_CreateObject();
            cJSON_AddBoolToObject(data, "restart_required", 1);
            cJSON_AddBoolToObject(data, "reboot_scheduled", reboot_requested);
            mqtt_publish_response(id, cmd, 1, 0, "configuration saved", data);
            mqtt_publish_status("config_saved", "configuration saved; restart required");
            if (reboot_requested)
                mqtt_reboot_pending = 1;
        }
        else
        {
            mqtt_publish_response(id, cmd, 0, 422, error, NULL);
        }
    }
    else if (strcmp(cmd, "reboot") == 0)
    {
        mqtt_publish_response(id, cmd, 1, 0, "reboot scheduled", NULL);
        mqtt_publish_status("rebooting", "reboot command accepted");
        mqtt_reboot_pending = 1;
    }
    else if ((strcmp(cmd, "real_set") == 0) ||
             (strcmp(cmd, "real_event") == 0) ||
             (strcmp(cmd, "sim_set") == 0) ||
             (strcmp(cmd, "sim_event") == 0))
    {
        if (real_board_business_handle_json_command(command_root, cmd, &module_data, &module_code, &module_message))
            mqtt_publish_response(id, cmd, module_code == 0, module_code, module_message, module_data);
        else
            mqtt_publish_response(id, cmd, 0, 404, "unknown real board command", NULL);
    }
    else if ((strcmp(cmd, "ota_begin") == 0) ||
             (strcmp(cmd, "ota_chunk") == 0) ||
             (strcmp(cmd, "ota_end") == 0) ||
             (strcmp(cmd, "ota_abort") == 0) ||
             (strcmp(cmd, "ota_status") == 0))
    {
        if (real_board_ota_handle_command(command_root, cmd, &module_data, &module_code, &module_message, &module_reboot_requested))
        {
            mqtt_publish_response(id, cmd, module_code == 0, module_code, module_message, module_data);
            if (module_reboot_requested)
            {
                mqtt_publish_status("ota_rebooting", "OTA image verified; rebooting for bootloader switch");
                mqtt_reboot_pending = 1;
            }
        }
        else
        {
            mqtt_publish_response(id, cmd, 0, 404, "unknown OTA command", NULL);
        }
    }
#if REAL_BOARD_ENABLE_LEGACY_FRAME
    else if (real_board_business_handle_legacy_command(command_root, cmd, &legacy_data, &legacy_code, &legacy_message, &legacy_reboot_requested))
    {
        mqtt_publish_response(id, cmd, legacy_code == 0, legacy_code, legacy_message, legacy_data);
        if (legacy_reboot_requested)
            mqtt_reboot_pending = 1;
    }
#endif
    else
    {
        mqtt_publish_response(id, cmd, 0, 404, "unknown command", NULL);
    }

    mqtt_delete_command_roots(root, command_root);
}

uint8_t mqtt_subscribe(char *topic, uint8_t qos)
{
    int rc = 0;
    int len = 0;

    topicString.cstring = topic;
    len = MQTTV5Serialize_subscribe(mqtt_buff, mqtt_buff_len, 0, msgid, &sub_properties, 1, &topicString, &qos, &sub_options);
    rc = mqtt_send(mqtt_buff, len);
    if (MQTTV5Packet_read(mqtt_buff, mqtt_buff_len, mqtt_recv) == SUBACK) /* wait for suback */
    {
        unsigned short submsgid;
        int subcount;
        unsigned char reason_code;

        rc = MQTTV5Deserialize_suback(&submsgid, &recv_properties, 1, &subcount, &reason_code, mqtt_buff, mqtt_buff_len);
        if (reason_code != 0)
        {
            printf("granted qos != 0, %d (%s)\n",
                   reason_code, reason_code <= 2 ? "Granted QoS" : v5reasoncode_to_string(reason_code));
            // goto exit;
        }

        printf("suback: (%d properties)\n", recv_properties.count);
    }
    return (uint8_t)rc;
}

uint8_t mqtt_connect()
{
    int rc = 0;
    int len = 0;

    MQTTProperties connack_properties = MQTTProperties_initializer;
    MQTTV5Packet_connectData data = MQTTV5Packet_connectData_initializer;
    MQTTProperties will_properties = MQTTProperties_initializer;
    data.clientID.cstring = system_config->device_id[0] != '\0' ? system_config->device_id : "GM400";
    data.keepAliveInterval = 60;
    data.cleanstart = 1;
    if (system_config->mqtt_server.username[0] != '\0')
        data.username.cstring = system_config->mqtt_server.username;
    if (system_config->mqtt_server.password[0] != '\0')
        data.password.cstring = system_config->mqtt_server.password;
    data.MQTTVersion = 5;
    data.willFlag = 1;
    data.will.qos = 0;
    data.will.retained = 1;
    data.will.topicName.cstring = (char *)mqtt_get_status_topic();
    data.will.message.cstring = mqtt_make_status_payload("offline", "mqtt_lwt");
    data.will.properties = &will_properties;

    MQTTProperties conn_properties = MQTTProperties_initializer;
    len = MQTTV5Serialize_connect(mqtt_buff, mqtt_buff_len, &data, &conn_properties);
    rc = mqtt_send(mqtt_buff, len);
    if (rc <= 0)
    {
        printf("mqtt_send failed: -0x%04x\n", -rc);
        mqtt_reset_connection();
        return 1;
    }

    printf("Sent MQTTv5 connect\n");
    /* wait for connack */
    if (MQTTV5Packet_read(mqtt_buff, mqtt_buff_len, mqtt_recv) == CONNACK)
    {
        unsigned char sessionPresent, connack_rc;

        if (MQTTV5Deserialize_connack(&connack_properties, &sessionPresent, &connack_rc, mqtt_buff, mqtt_buff_len) != 1 || connack_rc != 0)
        {
            printf("Unable to connect, return code %d\n", connack_rc);
            mqtt_reset_connection();
            return 1;
        }
    }
    else
    {
        printf("Failed to read CONNACK\n");
        mqtt_reset_connection();
        return 1;
    }

    printf("MQTTv5 connected: (%d properties)\n", connack_properties.count);

    // 订阅服务端下行主题；服务端可用 v1/devices/request/+ 向多设备广播匹配。
    mqtt_subscribe((char *)mqtt_get_request_topic(), system_config->mqtt_server.qos[MQTT_TOPIC_CMD_DOWN_INDEX]);

    MQTTProperty pub_properties_array[1];
    MQTTProperties pub_properties = MQTTProperties_initializer;
    pub_properties.array = pub_properties_array;
    pub_properties.max_count = 1;

    // 用户键值字段，不影响
    MQTTProperty v5property;
    v5property.identifier = MQTTPROPERTY_CODE_USER_PROPERTY;
    v5property.value.string_pair.key.data = "user key";
    v5property.value.string_pair.key.len = strlen(v5property.value.string_pair.key.data);
    v5property.value.string_pair.val.data = "user value";
    v5property.value.string_pair.val.len = strlen(v5property.value.string_pair.val.data);
    rc = MQTTProperties_add(&pub_properties, &v5property);
    if (rc)
    {
        printf("Failed to add user property\n");
    }

    send_properties.array = send_properties_array;
    send_properties.max_count = 2;
    mqtt_publish_lifecycle_status("online", "mqtt_connected");
    return 0;
}
static void mqtt_send_ack(unsigned char packet_type, unsigned char dup_flag, uint16_t packet_id)
{
    int len;
    unsigned char ack_buffer[8];

    MQTTProperties properties = MQTTProperties_initializer;
    properties.count = 0;
    properties.array = NULL;

    switch (packet_type)
    {
    case PUBACK:
        len = MQTTV5Serialize_puback(ack_buffer, sizeof(ack_buffer), packet_id, 0, &properties);
        break;
    case PUBREC:
        len = MQTTV5Serialize_pubrec(ack_buffer, sizeof(ack_buffer), packet_id, 0, &properties);
        break;
    case PUBREL:
        len = MQTTV5Serialize_pubrel(ack_buffer, sizeof(ack_buffer), dup_flag, packet_id, 0, &properties);
        break;
    case PUBCOMP:
        len = MQTTV5Serialize_pubcomp(ack_buffer, sizeof(ack_buffer), packet_id, 0, &properties);
        break;
    default:
        return;
    }

    if (len > 0)
    {
        mqtt_send(ack_buffer, len);
        printf("Sent MQTT ack type %d for msgid: %d\n", packet_type, packet_id);
    }
}

static void mqtt_handle_ack_packet(unsigned char expected_type)
{
    unsigned char packet_type = 0;
    unsigned char dup_flag = 0;
    unsigned char reason_code = 0;
    unsigned short packet_id = 0;
    MQTTProperties properties = MQTTProperties_initializer;

    if (MQTTV5Deserialize_ack(&packet_type, &dup_flag, &packet_id, &reason_code, &properties, mqtt_buff, mqtt_buff_len) != 1)
        return;

    if (packet_type != expected_type)
        return;

    if (packet_type == PUBREC)
    {
        mqtt_send_ack(PUBREL, 0, packet_id);
    }
    else if (packet_type == PUBREL)
    {
        mqtt_send_ack(PUBCOMP, 0, packet_id);
    }
    else
    {
        printf("Received MQTT ack type %d for msgid: %d reason: %d\n", packet_type, packet_id, reason_code);
    }
}

void mqtt_task()
{
    int32_t rc;
    int32_t payloadlen_in;
    uint8_t *payload_in;

    switch (state)
    {
    case MQTT_STATE_INIT:
        if (mqtt_init() == 0)
            state = MQTT_STATE_CONNECTING;
        break;
    case MQTT_STATE_CONNECTING:
        if (mqtt_connect() == 0)
            state = MQTT_STATE_CONNECTED;
        break;
    case MQTT_STATE_CONNECTED:
        rc = MQTTV5Packet_read(mqtt_buff, mqtt_buff_len, mqtt_recv);
        if (rc == PUBLISH)
        {
            rc = MQTTV5Deserialize_publish(&dup, &qos, &retained, &msgid, &receivedTopic, &recv_properties,
                                           &payload_in, &payloadlen_in, mqtt_buff, mqtt_buff_len);
            if (rc == 1)
            {
                printf("message arrived %.*s\n", (int)payloadlen_in, payload_in);
                if (qos == 1)
                    mqtt_send_ack(PUBACK, 0, msgid);
                else if (qos == 2)
                    mqtt_send_ack(PUBREC, 0, msgid);
                if (qos <= 2)
                    mqtt_handle_command_json(payload_in, payloadlen_in, mqtt_received_topic_matches(MQTT_TOPIC_OTA_INDEX));
                else
                    mqtt_publish_debug("invalid_qos", "command payload qos is invalid");
            }
        }
        else if (rc == PUBREC)
        {
            mqtt_handle_ack_packet(PUBREC);
        }
        else if (rc == PUBREL)
        {
            mqtt_handle_ack_packet(PUBREL);
        }
        else if (rc == PUBCOMP)
        {
            mqtt_handle_ack_packet(PUBCOMP);
        }
        else if (rc == PUBACK)
        {
            mqtt_handle_ack_packet(PUBACK);
        }

        static uint32_t tick = 0;
        tick++;
        if (tick >= MQTT_TELEMETRY_TICKS)
        {
            tick = 0;
            mqtt_publish_telemetry();
            mqtt_publish_real_event();
        }
        if (mqtt_reboot_pending)
        {
            delay_ms(200);
            NVIC_SystemReset();
        }
        // delay_ms(1000);
        break;
    default:
        break;
    }
}

#include "nmbs/port.h"
static nmbs_t nmbs;
static nmbs_server_t nmbs_server = {
    .id = 0x01,
    .coils =
        {
            0,
        },
    .regs =
        {
            0,
        },
};


void modbus_task()
{
    uint8_t save_requested;
    uint8_t reset_requested;
    nmbs_error poll_result;

    poll_result = nmbs_server_poll(&nmbs);
    if (poll_result == NMBS_ERROR_TIMEOUT)
        return;
    if ((poll_result == NMBS_ERROR_TRANSPORT) || (poll_result == NMBS_ERROR_INVALID_TCP_MBAP))
    {
        disconnect(w5500_socket_modbus.sn);
        close(w5500_socket_modbus.sn);
        w5500_socket_modbus.state = SOCK_CLOSED;
        return;
    }

    save_requested = nmbs_bitfield_read(nmbs_server.coils, 0);
    reset_requested = nmbs_bitfield_read(nmbs_server.coils, 1);

    if (save_requested)
    {
        memcpy(&system_config_temp, nmbs_server.regs, sizeof(system_config_t));
        if (config_save(&system_config_temp))
        {
            memcpy(nmbs_server.regs, &system_config_temp, sizeof(system_config_t));
        }
        else
        {
            system_config_temp = *system_config;
            memcpy(nmbs_server.regs, &system_config_temp, sizeof(system_config_t));
        }
        nmbs_bitfield_write(nmbs_server.coils, 0, 0);
    }

    if (reset_requested)
    {
        nmbs_bitfield_write(nmbs_server.coils, 1, 0);
        NVIC_SystemReset();
    }
}

int main()
{
    LL_PERIPH_WE(LL_PERIPH_ALL);
    GPIO_SetDebugPort(GPIO_PIN_TDO | GPIO_PIN_TDI | GPIO_PIN_TRST, DISABLE); // 关闭多余调试口

    sysclk_init();
    crc_init();
    trng_init();
    led_init();
    real_board_lamp_init();
    timer0_1b_init();

    usart1_init();
    printf("hello\r\n");

    // 初始化系统默认配置
    config_check();
    real_board_business_init();
    real_board_hardware_init();
    real_board_ota_init();

    // 设置spi驱动
    spi_init();
    w5500_init();
    real_board_ntp_init();

    // uint32_t crc;
    // crc = crc_block_calculate((uint8_t *)system_config, sizeof(system_config) - 4);
    // if (crc != system_config->crc)
    // {
    //     printf("系统配置校验错误\r\n");
    // }

    w5500_socket_mqtt.buff = malloc(2048);
    w5500_socket_modbus.buff = malloc(2048);

    memcpy(nmbs_server.regs, &system_config_temp, sizeof(system_config_t));

    nmbs_server_init(&nmbs, &nmbs_server);

    while (1)
    {
        w5500_modbus_task();
        real_board_ntp_task();
        if (!mqtt_backoff_task())
        {
            w5500_mqtt_client_task();
            if (w5500_socket_mqtt.state == SOCK_ESTABLISHED)
                mqtt_task();
        }
        if (w5500_socket_modbus.state == SOCK_ESTABLISHED)
            modbus_task();
        real_board_hardware_task();
        real_board_business_tick();
        led_task();
    }
    return 0;
}
