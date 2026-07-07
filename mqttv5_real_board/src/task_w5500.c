#include "task_w5500.h"
#include "wiz_interface.h"
#include "dns.h"

#include "string.h"
#include "stdio.h"

#include "config.h"
#define W5500_DEBUG printf
#define DATA_BUF_SIZE 2048
#define W5500_SOCKET_COUNT 8U
#define W5500_TCP_SERVER_IDLE_TICKS 5000U

w5500_socket_t w5500_socket_mqtt = {
    .sn = 0,
    .mode = W5500_SOCKET_MODE_CLIENT,
    .port = 1883,
    .ip = {192, 168, 0, 110},
};
w5500_socket_t w5500_socket_modbus = {
    .sn = 1,
    .mode = W5500_SOCKET_MODE_SERVER,
    .port = 502,
};

#define ETHERNET_BUF_MAX_SIZE (1024 * 2)
static uint8_t ethernet_buf[ETHERNET_BUF_MAX_SIZE] = {0};
static wiz_NetInfo default_net_info;
static uint16_t server_idle_ticks[W5500_SOCKET_COUNT] = {0};

void w5500_dns(w5500_socket_t *w5500_socket, uint8_t *domain_name, uint8_t *domain_ip);

static uint8_t parse_ipv4_address(const char *text, uint8_t ip[4])
{
    uint16_t value = 0;
    uint8_t octet = 0;
    uint8_t has_digit = 0;

    if ((text == NULL) || (text[0] == '\0'))
        return 0;

    for (uint16_t i = 0;; i++)
    {
        char ch = text[i];
        if ((ch >= '0') && (ch <= '9'))
        {
            has_digit = 1;
            value = (uint16_t)(value * 10U + (uint16_t)(ch - '0'));
            if (value > 255U)
                return 0;
        }
        else if ((ch == '.') || (ch == '\0'))
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

static void w5500_apply_mqtt_server_config(void)
{
    uint8_t resolved_ip[4];

    w5500_socket_mqtt.port = system_config->mqtt_server.port;
    if (w5500_socket_mqtt.port == 0)
        w5500_socket_mqtt.port = 1883;

    if (parse_ipv4_address(system_config->mqtt_server.host, resolved_ip))
    {
        memcpy(w5500_socket_mqtt.ip, resolved_ip, sizeof(w5500_socket_mqtt.ip));
        return;
    }

    if (system_config->mqtt_server.host[0] != '\0')
    {
        w5500_dns(&w5500_socket_mqtt, (uint8_t *)system_config->mqtt_server.host, resolved_ip);
        memcpy(w5500_socket_mqtt.ip, resolved_ip, sizeof(w5500_socket_mqtt.ip));
    }
}

static void w5500_reset_server_idle(w5500_socket_t *w5500_socket)
{
    if (w5500_socket->sn < W5500_SOCKET_COUNT)
        server_idle_ticks[w5500_socket->sn] = 0;
}

static uint8_t w5500_server_idle_expired(w5500_socket_t *w5500_socket)
{
    if (w5500_socket->sn >= W5500_SOCKET_COUNT)
        return 0;

    if (server_idle_ticks[w5500_socket->sn] < W5500_TCP_SERVER_IDLE_TICKS)
        server_idle_ticks[w5500_socket->sn]++;
    return server_idle_ticks[w5500_socket->sn] >= W5500_TCP_SERVER_IDLE_TICKS;
}

void w5500_tcp_server(w5500_socket_t *w5500_socket)
{
    int32_t ret;
    uint16_t size;
    uint8_t ir;
    uint8_t destip[4];
    uint16_t destport;

    w5500_socket->state = getSn_SR(w5500_socket->sn);
    switch (w5500_socket->state)
    {
    case SOCK_ESTABLISHED:
        ir = getSn_IR(w5500_socket->sn);
        if (ir & (Sn_IR_DISCON | Sn_IR_TIMEOUT))
        {
            setSn_IR(w5500_socket->sn, ir & (Sn_IR_DISCON | Sn_IR_TIMEOUT));
            w5500_reset_server_idle(w5500_socket);
            if ((ret = disconnect(w5500_socket->sn)) != SOCK_OK)
                close(w5500_socket->sn);
            W5500_DEBUG("Socket %d:Disconnected by interrupt 0x%02X\r\n", w5500_socket->sn, ir);
            return;
        }
        if (ir & Sn_IR_CON)
        {
            getSn_DIPR(w5500_socket->sn, destip);
            destport = getSn_DPORT(w5500_socket->sn);
            W5500_DEBUG("Socket %d:Connected - %d.%d.%d.%d : %d\r\n", w5500_socket->sn, destip[0], destip[1], destip[2], destip[3], destport);

            setSn_IR(w5500_socket->sn, Sn_IR_CON);
            w5500_reset_server_idle(w5500_socket);
        }
        size = getSn_RX_RSR(w5500_socket->sn);
        if (size > 0) // Don't need to check SOCKERR_BUSY because it doesn't not occur.
        {
            w5500_reset_server_idle(w5500_socket);
        }
        else if (w5500_server_idle_expired(w5500_socket))
        {
            W5500_DEBUG("Socket %d:Idle timeout\r\n", w5500_socket->sn);
            if ((ret = disconnect(w5500_socket->sn)) != SOCK_OK)
                close(w5500_socket->sn);
            w5500_reset_server_idle(w5500_socket);
            return;
        }
        break;
    case SOCK_CLOSE_WAIT:
        w5500_reset_server_idle(w5500_socket);
        if ((ret = disconnect(w5500_socket->sn)) != SOCK_OK)
            W5500_DEBUG("Socket %d:Disconnect failed in CLOSE_WAIT (%ld)\r\n", w5500_socket->sn, ret);
        close(w5500_socket->sn);
        W5500_DEBUG("Socket %d:Closed\r\n", w5500_socket->sn);
        break;
    case SOCK_FIN_WAIT:
    case SOCK_CLOSING:
    case SOCK_TIME_WAIT:
    case SOCK_LAST_ACK:
        w5500_reset_server_idle(w5500_socket);
        close(w5500_socket->sn);
        W5500_DEBUG("Socket %d:Force close from state 0x%02X\r\n", w5500_socket->sn, w5500_socket->state);
        break;
    case SOCK_INIT:
        w5500_reset_server_idle(w5500_socket);
        W5500_DEBUG("Socket %d:Listen, TCP server loopback, port [%d]\r\n", w5500_socket->sn, w5500_socket->port);
        if ((ret = listen(w5500_socket->sn)) != SOCK_OK)
            return;
        break;
    case SOCK_CLOSED:
        w5500_reset_server_idle(w5500_socket);
        ret = socket(w5500_socket->sn, Sn_MR_TCP, w5500_socket->port, SF_IO_NONBLOCK);
        if (ret != w5500_socket->sn)
            return;
        break;
    default:
        break;
    }
    return;
}

void w5500_tcp_client(w5500_socket_t *w5500_socket)
{
    int32_t ret; // return value for SOCK_ERRORs

    // Socket Status Transitions
    // Check the W5500 Socket n status register (Sn_SR, The 'Sn_SR' controlled by Sn_CR command or Packet send/recv status)11
    w5500_socket->state = getSn_SR(w5500_socket->sn);
    switch (w5500_socket->state)
    {
    case SOCK_CLOSED:
        close(w5500_socket->sn);
        W5500_DEBUG("Socket %d Closed\r\n", w5500_socket->sn);

        for (uint16_t port = 50000;; port++)
        {
            ret = socket(w5500_socket->sn, Sn_MR_TCP, port, SF_IO_NONBLOCK);
            if (ret == w5500_socket->sn)
            {
                W5500_DEBUG("Socket %d Creat, Port : %d\r\n", w5500_socket->sn, port);
                break;
            }
            if (port >= 60000)
            {
                W5500_DEBUG("Socket %d Creat,Err\r\n", w5500_socket->sn);
                break;
            }
        }

        break;
    case SOCK_INIT:
        W5500_DEBUG("Socket %d:Try to connect to the %d.%d.%d.%d : %d\r\n", w5500_socket->sn, w5500_socket->ip[0], w5500_socket->ip[1], w5500_socket->ip[2], w5500_socket->ip[3], w5500_socket->port);
        for (uint8_t err = 0;; err++)
        {
            ret = connect(w5500_socket->sn, w5500_socket->ip, w5500_socket->port);
            if (ret == SOCK_OK)
                break;
            if (err >= 5)
            {
                W5500_DEBUG("Socket %d:connect to the %d.%d.%d.%d : %d err\r\n", w5500_socket->sn, w5500_socket->ip[0], w5500_socket->ip[1], w5500_socket->ip[2], w5500_socket->ip[3], w5500_socket->port);
                return;
            }
        }
        break;
    case SOCK_ESTABLISHED:
        if (getSn_IR(w5500_socket->sn) & Sn_IR_CON) // Socket n interrupt register mask; TCP CON interrupt = connection with peer is successful
        {
            W5500_DEBUG("Socket %d:Connected to - %d.%d.%d.%d : %d\r\n", w5500_socket->sn, w5500_socket->ip[0], w5500_socket->ip[1], w5500_socket->ip[2], w5500_socket->ip[3], w5500_socket->port);
            setSn_IR(w5500_socket->sn, Sn_IR_CON); // this interrupt should be write the bit cleared to '1'
            setSn_KPALVTR(w5500_socket->sn, 6);    // 30s keepalive
        }

        //////////////////////////////////////////////////////////////////////////////////////////////
        // Data Transaction Parts; Handle the [data receive and send] process
        //////////////////////////////////////////////////////////////////////////////////////////////
        // if ((size = getSn_RX_RSR(sn)) > 0) // Sn_RX_RSR: Socket n Received Size Register, Receiving data length
        // {
        //     if (size > DATA_BUF_SIZE)
        //         size = DATA_BUF_SIZE;  // DATA_BUF_SIZE means user defined buffer size (array)
        //     ret = recv(sn, buf, size); // Data Receive process (H/W Rx socket buffer -> User's buffer)
        //     buf[ret] = 0x00;           // Add a string terminator
        //     printf("recv: %s\n", buf); // print the receive data
        //     if (ret <= 0)
        //         return ret; // If the received data length <= 0, receive failed and process end
        //     size = (uint16_t)ret;
        //     sentsize = 0;

        //     // Data sentsize control
        //     while (size != sentsize)
        //     {
        //         ret = send(sn, buf + sentsize, size - sentsize); // Data send process (User's buffer -> Destination through H/W Tx socket buffer)
        //         if (ret < 0)                                     // Send Error occurred (sent data length < 0)
        //         {
        //             close(sn); // socket close
        //             return ret;
        //         }
        //         sentsize += ret; // Don't care SOCKERR_BUSY, because it is zero.
        //     }
        // }
        //////////////////////////////////////////////////////////////////////////////////////////////
        break;

    case SOCK_CLOSE_WAIT:
        W5500_DEBUG("Socket %d:Disonnected\r\n", w5500_socket->sn);
        if ((ret = disconnect(w5500_socket->sn)) != SOCK_OK)
            return;
        break;

    default:
        break;
    }
}
void w5500_dns(w5500_socket_t *w5500_socket, uint8_t *domain_name, uint8_t *domain_ip)
{
    int ret;
    DNS_init(w5500_socket->sn, w5500_socket->buff); // DNS client init

    for (uint32_t timeout = 0;; timeout++)
    {
        ret = DNS_run(default_net_info.dns, domain_name, domain_ip);
        if (ret == 1)
        {
            printf("> Translated %s to %d.%d.%d.%d\r\n", domain_name, domain_ip[0], domain_ip[1], domain_ip[2], domain_ip[3]);
            break;
        }
        if (ret == 100)
        {
            printf("> DNS Failed\r\n");
            break;
        }
        if (timeout >= 1000U)
        {
            printf("> DNS Timeout\r\n");
            break;
        }
    }
}

void w5500_init()
{
    wizchip_initialize();

    memcpy(default_net_info.ip, system_config->eth.ip, 4);
    memcpy(default_net_info.sn, system_config->eth.sn, 4);
    memcpy(default_net_info.gw, system_config->eth.gw, 4);
    memcpy(default_net_info.dns, system_config->eth.dns, 4);
    memcpy(default_net_info.mac, system_config->eth.mac, 6);
    if (system_config->eth.mode)
        default_net_info.dhcp = NETINFO_DHCP;
    else
        default_net_info.dhcp = NETINFO_STATIC;
}

static uint8_t w5500_prepare_network(void)
{
    uint8_t phy_link_status;
    static uint8_t first_config = 1;
    static uint8_t phy_link_was_on = 0;

    ctlwizchip(CW_GET_PHYLINK, (void *)&phy_link_status);

    if (phy_link_status == PHY_LINK_ON)
    {
        if (first_config || !phy_link_was_on)
        {
            first_config = 0;
            phy_link_was_on = 1;
            network_init(ethernet_buf, (wiz_NetInfo *)&default_net_info);
            w5500_apply_mqtt_server_config();
        }
    }
    else
    {
        if (phy_link_was_on || first_config)
            W5500_DEBUG("PHY no link\r\n");
        phy_link_was_on = 0;
        if (first_config)
            return 0;
    }

    return 1;
}

// int inet_pton(int af, const char *src, void *dst);
void w5500_task()
{
    if (!w5500_prepare_network())
        return;

    w5500_tcp_client(&w5500_socket_mqtt);
    w5500_tcp_server(&w5500_socket_modbus);
}

void w5500_modbus_task()
{
    if (!w5500_prepare_network())
        return;

    w5500_tcp_server(&w5500_socket_modbus);
}

void w5500_mqtt_client_task()
{
    if (!w5500_prepare_network())
        return;

    w5500_tcp_client(&w5500_socket_mqtt);
}
