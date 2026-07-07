#include "real_board_ntp.h"

#include "config.h"
#include "sntp.h"
#include "socket.h"

#include "stdio.h"
#include "string.h"

#define REAL_BOARD_NTP_SOCKET 7U
#define REAL_BOARD_NTP_TIMEZONE_CN 39U
#define REAL_BOARD_NTP_POLL_DIVIDER 2000U
#define REAL_BOARD_NTP_TIMEOUT_TICKS 20000UL
#define REAL_BOARD_NTP_RESYNC_TICKS 3600000UL
#define REAL_BOARD_NTP_DNS_RETRY_TICKS 60000UL

typedef enum
{
    REAL_BOARD_NTP_STATE_IDLE = 0,
    REAL_BOARD_NTP_STATE_RESOLVING,
    REAL_BOARD_NTP_STATE_RUNNING,
    REAL_BOARD_NTP_STATE_SYNCED,
    REAL_BOARD_NTP_STATE_FAILED
} real_board_ntp_state_t;

typedef struct
{
    real_board_ntp_state_t state;
    uint8_t server_ip[4];
    uint8_t synced;
    uint32_t task_ticks;
    uint32_t state_ticks;
    uint32_t last_sync_tick;
    datetime last_time;
    char last_error[48];
} real_board_ntp_context_t;

static uint8_t ntp_buf[MAX_SNTP_BUF_SIZE];
static real_board_ntp_context_t ntp_ctx;

static uint8_t parse_ipv4_address(const char *text, uint8_t ip[4])
{
    uint16_t value = 0;
    uint8_t octet = 0;
    uint8_t has_digit = 0;

    if (text == NULL || text[0] == '\0')
        return 0;

    for (uint16_t i = 0;; i++)
    {
        char ch = text[i];
        if (ch >= '0' && ch <= '9')
        {
            has_digit = 1U;
            value = (uint16_t)(value * 10U + (uint16_t)(ch - '0'));
            if (value > 255U)
                return 0;
        }
        else if (ch == '.' || ch == '\0')
        {
            if (!has_digit || octet >= 4U)
                return 0;
            ip[octet++] = (uint8_t)value;
            value = 0;
            has_digit = 0;
            if (ch == '\0')
                return octet == 4U;
        }
        else
        {
            return 0;
        }
    }
}

static const char *state_text(void)
{
    switch (ntp_ctx.state)
    {
    case REAL_BOARD_NTP_STATE_RESOLVING:
        return "resolving";
    case REAL_BOARD_NTP_STATE_RUNNING:
        return "running";
    case REAL_BOARD_NTP_STATE_SYNCED:
        return "synced";
    case REAL_BOARD_NTP_STATE_FAILED:
        return "failed";
    default:
        return "idle";
    }
}

static void set_error(const char *message)
{
    snprintf(ntp_ctx.last_error, sizeof(ntp_ctx.last_error), "%s", message);
}

static void start_sync(void)
{
    if (system_config->mqtt_server.ntp_server[0] == '\0')
    {
        ntp_ctx.state = REAL_BOARD_NTP_STATE_FAILED;
        set_error("NTP server is empty");
        return;
    }

    ntp_ctx.state = REAL_BOARD_NTP_STATE_RESOLVING;
    ntp_ctx.state_ticks = 0;
}

static void resolve_server(void)
{
    if (!parse_ipv4_address(system_config->mqtt_server.ntp_server, ntp_ctx.server_ip))
    {
        ntp_ctx.state = REAL_BOARD_NTP_STATE_FAILED;
        ntp_ctx.state_ticks = 0;
        set_error("NTP DNS deferred");
        return;
    }

    SNTP_init(REAL_BOARD_NTP_SOCKET, ntp_ctx.server_ip, REAL_BOARD_NTP_TIMEZONE_CN, ntp_buf);
    ntp_ctx.state = REAL_BOARD_NTP_STATE_RUNNING;
    ntp_ctx.state_ticks = 0;
}

void real_board_ntp_init(void)
{
    memset(&ntp_ctx, 0, sizeof(ntp_ctx));
    start_sync();
}

void real_board_ntp_task(void)
{
    ntp_ctx.task_ticks++;
    if ((ntp_ctx.task_ticks % REAL_BOARD_NTP_POLL_DIVIDER) != 0U)
        return;

    ntp_ctx.state_ticks += REAL_BOARD_NTP_POLL_DIVIDER;

    if (ntp_ctx.state == REAL_BOARD_NTP_STATE_SYNCED &&
        (ntp_ctx.task_ticks - ntp_ctx.last_sync_tick) >= REAL_BOARD_NTP_RESYNC_TICKS)
    {
        start_sync();
    }

    if (ntp_ctx.state == REAL_BOARD_NTP_STATE_IDLE)
    {
        start_sync();
    }
    else if (ntp_ctx.state == REAL_BOARD_NTP_STATE_RESOLVING)
    {
        resolve_server();
    }
    else if (ntp_ctx.state == REAL_BOARD_NTP_STATE_RUNNING)
    {
        if (SNTP_run(&ntp_ctx.last_time) == 1)
        {
            ntp_ctx.synced = 1U;
            ntp_ctx.last_sync_tick = ntp_ctx.task_ticks;
            ntp_ctx.state = REAL_BOARD_NTP_STATE_SYNCED;
            ntp_ctx.state_ticks = 0;
            ntp_ctx.last_error[0] = '\0';
        }
        else if (ntp_ctx.state_ticks >= REAL_BOARD_NTP_TIMEOUT_TICKS)
        {
            close(REAL_BOARD_NTP_SOCKET);
            ntp_ctx.state = REAL_BOARD_NTP_STATE_FAILED;
            set_error("NTP sync timeout");
        }
    }
    else if (ntp_ctx.state == REAL_BOARD_NTP_STATE_FAILED &&
             ntp_ctx.state_ticks >= REAL_BOARD_NTP_DNS_RETRY_TICKS)
    {
        start_sync();
    }
}

uint8_t real_board_ntp_is_synced(void)
{
    return ntp_ctx.synced;
}

void real_board_ntp_append_status(cJSON *object)
{
    cJSON *ntp = cJSON_AddObjectToObject(object, "ntp");
    if (ntp == NULL)
        return;

    cJSON_AddStringToObject(ntp, "server", system_config->mqtt_server.ntp_server);
    cJSON_AddStringToObject(ntp, "state", state_text());
    cJSON_AddBoolToObject(ntp, "synced", ntp_ctx.synced);
    cJSON_AddNumberToObject(ntp, "server_ip_0", ntp_ctx.server_ip[0]);
    cJSON_AddNumberToObject(ntp, "server_ip_1", ntp_ctx.server_ip[1]);
    cJSON_AddNumberToObject(ntp, "server_ip_2", ntp_ctx.server_ip[2]);
    cJSON_AddNumberToObject(ntp, "server_ip_3", ntp_ctx.server_ip[3]);
    cJSON_AddNumberToObject(ntp, "year", ntp_ctx.last_time.yy);
    cJSON_AddNumberToObject(ntp, "month", ntp_ctx.last_time.mo);
    cJSON_AddNumberToObject(ntp, "day", ntp_ctx.last_time.dd);
    cJSON_AddNumberToObject(ntp, "hour", ntp_ctx.last_time.hh);
    cJSON_AddNumberToObject(ntp, "minute", ntp_ctx.last_time.mm);
    cJSON_AddNumberToObject(ntp, "second", ntp_ctx.last_time.ss);
    cJSON_AddStringToObject(ntp, "last_error", ntp_ctx.last_error);
}
