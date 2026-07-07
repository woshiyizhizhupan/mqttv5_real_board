# 真实板卡工程代码变更与需求差距清单

时间：2026-06-29

范围：

- 新工程：`D:\code\唐家湾嵌入式兼职\mqttv5_real_board`
- 旧业务参考：`D:\code\唐家湾嵌入式兼职\资料`
- 上位机：`D:\code\唐家湾嵌入式兼职\mqttv5_tool`

## 1. 旧业务协议兼容变更

### 1.1 旧帧类型和命令号承接

文件：`mqttv5_real_board\src\real_board_business.h`

- 第 7 行：保留旧前导字节 `REAL_BOARD_FRM_PREAMBLE = 0xFE`。
- 第 8-12 行：恢复旧业务帧类型：
  - `0xA1`：主板参数设置/工厂设置。
  - `0xA2`：主板控制。
  - `0xA3`：调试/异常。
  - `0xA4`：升级。
  - `0xA5`：外设透传。
- 第 14-20 行：当前明确实现的 A1/A2 命令：
  - `0x09`：读 CPU ID。
  - `0x0A`：设置 ProductKey。
  - `0x0B`：设置 DeviceName，并映射为 MQTT5 `device_id`。
  - `0x0C`：设置 DeviceSecret。
  - `0xFA`：设置 MQTT endpoint，格式为 `host,port`。
  - `0xFB`：接收旧 KNS payload。
  - `0x01`：A2 重启。
- 第 21-22 行：A5 外设等待队列参数：
  - 最大挂起槽位 `REAL_BOARD_A5_PENDING_MAX = 4`。
  - 最大外设响应缓存 `REAL_BOARD_A5_RESPONSE_MAX = 96`。

旧工程对应位置：

- `资料\user\user\protocol.h:13-17` 定义 A1-A5。
- `资料\user\user\protocol.h:20-62` 定义 A1/A2/A5 命令和 ACK。

### 1.2 旧 JSON 外壳下行解析

文件：`mqttv5_real_board\src\real_board_business.c`

- 第 4 行：引入 `mbedtls/base64.h`，使用 Base64 承载旧二进制帧。
- 第 159-185 行：`find_legacy_frame_text()` 兼容多个旧/新字段：
  - 顶层 `frame`。
  - `params.frame`。
  - `params.params`，对应旧阿里云属性上报里的 `{"params":{"params":"base64..."}}`。
  - `data.frame`。
- 第 188-199 行：`decode_legacy_frame()` 使用 `mbedtls_base64_decode()` 解旧业务帧。
- 第 202-213 行：`encode_legacy_frame()` 使用 `mbedtls_base64_encode()` 回包。

旧工程对应位置：

- `资料\user\user\MQTT_App.c:164-190`：`Get_PayloadForm_Josn()` 从 JSON 中取 `frame` 并 Base64 解码。
- `资料\user\user\protocol.c:206-248`：`MQTT_Encode_Send()` 把二进制业务帧 Base64 后放进 JSON。

### 1.3 旧二进制帧解析

文件：`mqttv5_real_board\src\real_board_business.c`

- 第 216-272 行：`parse_legacy_frame()` 解析旧二进制帧：
  - 跳过一个或多个 `0xFE` 前导字节。
  - 读取 `frm_type`。
  - 读取 `cmd_type`。
  - 读取 2 字节大端 `payload_len`。
  - 指向 payload。
  - 计算 CRC32。
  - 同时兼容 CRC 大端/小端比较。
- 第 255-259 行：校验实际长度必须等于 `1 + 1 + 2 + payload_len + 4`，不允许半帧。

旧工程对应位置：

- `资料\user\user\protocol.c:351-380`：`Protocol_Frm_Decode()`。
- `资料\user\user\protocol.c:383-413`：`Protocol_Payload_Frm_Decode()`。

### 1.4 旧同步响应帧格式修正

文件：`mqttv5_real_board\src\real_board_business.c`

- 第 13 行：新增 `REAL_BOARD_LEGACY_SYNC_RESPONSE_CMD = 0x81`。
- 第 275-301 行：`build_legacy_response_frame()` 统一组外层响应帧：
  - `FE A5 81 len_hi len_lo payload crc32`
  - CRC 从 `A5` 开始算，不包含第一个 `FE`。
- 第 317 行：旧业务响应固定使用 `0x81`，不再使用此前的 `0x80 | 原cmd`。

旧工程对应位置：

- `资料\user\user\protocol.c:291-308`：`SynHandleUPSend()` 固定组 `FE A5 (0x80|Port)`。
- `资料\user\user\protocol.c:601` 和 `protocol.c:622`：A1/A2 调用 `SynHandleUPSend(..., 0x01, ...)`，所以旧同步响应 cmd 为 `0x81`。

### 1.5 旧响应 JSON 别名

文件：`mqttv5_real_board\src\real_board_business.c`

- 第 304-337 行：`create_legacy_response_data()` 生成旧业务响应对象。
- 第 326-330 行：成功编码响应帧后，同时输出：
  - `data.frame = base64_response`
  - `data.msg = base64_response`
- 这样新服务端可以读 `frame`，旧服务端可按旧 `data.msg` 习惯读。

文件：`mqttv5_real_board\src\main.c`

- 第 450-480 行：`mqtt_publish_response()` 统一包装响应。
- 第 461-466 行：如果 `data.legacy_protocol == true` 且 `ok=true`，顶层 `code` 转为 `200`，贴近旧阿里云同步响应：
  - 旧：`{"code":200,"data":{"msg":"..."},...}`
  - 新兼容：保留 `schema/device_id/cmd/ok/message`，但旧业务成功时 `code=200`。

## 2. A1/A2/A5 业务处理变更

### 2.1 A1 参数/工厂设置

文件：`mqttv5_real_board\src\real_board_business.c`

- 第 529-601 行：`handle_a1_command()`。
- 第 537-540 行：`0x09` 读 CPU ID，当前用 `device_id` CRC + MAC + `0x4005` 生成 13 字节返回。
- 第 542-547 行：`0x0A` 设置 ProductKey，保存到运行态 `legacy_product_key`，并回显 payload。
- 第 549-557 行：`0x0B` 设置 DeviceName：
  - 保存到运行态 `legacy_device_name`。
  - 写入 `system_config_temp.device_id`。
  - 调 `config_save()` 持久化。
  - 请求重启。
- 第 559-564 行：`0x0C` 设置 DeviceSecret，仅保存到运行态 `legacy_device_secret`，返回 `0x00`。
- 第 566-580 行：`0xFA` 设置 endpoint：
  - payload 格式要求 `host,port`。
  - 写入 `system_config_temp.mqtt_server.host`。
  - 写入 `system_config_temp.mqtt_server.port`。
  - 调 `save_current_config()` 保存并请求重启。
- 第 582-587 行：`0xFB` 接收旧 ProductKey/DeviceName/Secret 组合 payload，目前保存到运行态兼容字段。
- 第 589-590 行：未知 A1 命令不报错，进入 `create_legacy_received_data()`，返回 received。

### 2.2 A2 主板控制

文件：`mqttv5_real_board\src\real_board_business.c`

- 第 604-642 行：`handle_a2_command()`。
- 第 608-611 行：`0x01` 重启，设置 `reboot_requested = 1`。
- 第 612-616 行：`0x02` 所有设备电源，映射到 `relay1_on/relay2_on`。
- 第 617-621 行：`0x03` 灯电源，映射到 `relay1_on/lamp1_brightness`。
- 第 622-625 行：`0x0D` 继电器 1。
- 第 626-629 行：`0x0E` 继电器 2。
- 第 630-631 行：未知 A2 命令不报错，返回 received。

旧工程对应位置：

- `资料\user\user\protocol.c:605-623`：旧 `Protocol_A2_Ctr()` 当前只明确处理 `0x01` 重启，其它控制多由外设/业务层延伸。

### 2.3 A5 外设透传/等待队列

文件：`mqttv5_real_board\src\real_board_business.c`

- 第 23-39 行：定义 A5 槽位状态：
  - empty。
  - pending。
  - response。
  - timeout。
- 第 368-388 行：`find_a5_slot()` 查找/复用槽位。
- 第 390-403 行：`queue_a5_request()` 保存 A5 请求。
- 第 405-427 行：`append_a5_pending()` 把挂起队列放入 telemetry/status。
- 第 645-666 行：`handle_a5_command()`：
  - 记录 `last_peripheral_cmd`。
  - 记录 payload 长度。
  - 进入 pending 队列。
  - 响应 action 为 `outside_device_passthrough`。
  - 响应里带 `state/pending_slot/timeout_ticks`。
- 第 681-700 行：`real_board_business_tick()` 中对 pending 超时处理，超时门限 `REAL_BOARD_A5_TIMEOUT_TICKS = 300`。
- 第 756-771 行：`real_board_business_handle_peripheral_response()` 预留真实外设回调入口，用于把外设响应写回 pending slot。

## 3. 真实业务状态与上报字段

文件：`mqttv5_real_board\src\real_board_business.c`

- 第 42-103 行：`real_board_state` 承载真实业务状态：
  - 运行 tick、地址、keepalive、温度、错误码。
  - HLW8112 电表数据。
  - 环境采集数据。
  - RS485 状态。
  - 灯/继电器状态。
  - 工厂测试状态。
  - 旧 ProductKey/DeviceName/DeviceSecret 运行态字段。
  - 待上报事件。
- 第 669-679 行：`real_board_business_init()` 初始化默认业务状态：
  - `address=1`
  - `keepalive_s=300`
  - `rs485_read_period_s=30`
  - `device_type=0x4005`
- 第 703-720 行：`real_board_business_update_meter_hlw8112()`，预留 HLW8112 真实驱动接入口。
- 第 722-736 行：`real_board_business_update_environment()`，预留环境传感器接入口。
- 第 738-754 行：`real_board_business_update_rs485()`，预留 RS485 接入口。
- 第 774-861 行：`real_board_business_append_telemetry()` 生成真实板卡 telemetry JSON：
  - `schema = emqx-gateway.realboard.telemetry.v1`
  - `device_id`
  - `firmware_version`
  - `business_mode = real_board`
  - `meter_hlw8112`
  - `environment`
  - `rs485`
  - `lighting`
  - `device`
  - `peripherals`
  - `factory_test`
  - `legacy_protocol`
- 第 863-879 行：`real_board_business_append_status()` 生成状态摘要，供 `get_status` 返回。

注意：目前这三类真实采集函数没有被主循环或驱动层调用。也就是说字段和接入口已经有，但真实 HLW8112/环境/RS485 采样还没有接入。

## 4. MQTT5 JSON 层变更

文件：`mqttv5_real_board\src\main.c`

- 第 303-312 行：定义 5 个主题索引：
  - telemetry up。
  - command down。
  - event up。
  - OTA。
  - debug up。
- 第 414-425 行：`mqtt_publish_json()` 统一用 MQTT5 publish 发送 JSON。
- 第 427-448 行：`mqtt_add_config_json()` 把配置序列化到 JSON，包含：
  - `device_id`
  - `host`
  - `port`
  - `username`
  - `password`
  - `ntp_server`
  - `tls_mode`
  - `tls_verify_peer`
  - `topics`
  - `qos`
- 第 516-527 行：`mqtt_publish_telemetry()` 定期上报真实业务 telemetry。
- 第 529-542 行：`mqtt_publish_real_event()` 上报事件/告警。
- 第 580-676 行：`mqtt_apply_config_json()` 支持 MQTT 下发配置：
  - 修改 device_id/host/username/password/ntp_server。
  - 修改 port。
  - 修改 tls_mode。
  - 修改 tls_verify_peer。
  - 修改五个 topics。
  - 修改五个 qos。
  - 可带 reboot。
  - 最终 `config_save()` 持久化。
- 第 677-808 行：`mqtt_handle_command_json()` 分发下行命令：
  - 无 `cmd` 字段时尝试按旧业务帧处理。
  - `ping`
  - `get_status`
  - `get_config`
  - `set_config`
  - `reboot`
  - `real_set`
  - `real_event`
  - 兼容旧名 `sim_set/sim_event`
  - `ota_begin/ota_chunk/ota_end/ota_abort/ota_status`
  - 其它带旧业务帧字段的命令进入 `real_board_business_handle_legacy_command()`。
- 第 839-900 行：MQTT5 connect：
  - 使用 `MQTTVersion=5`。
  - 使用 `system_config->device_id` 作为 client id。
  - 支持 username/password。
  - 连接后订阅命令主题和 OTA 主题。
- 第 984-1062 行：`mqtt_task()`：
  - 处理 PUBLISH。
  - 按 QoS 发送 PUBACK/PUBREC。
  - 周期 telemetry。
  - 周期 event。
  - 处理重启 pending。

## 5. 主题和配置结构变更

文件：`mqttv5_real_board\src\config.h`

- 第 9 行：`MQTT_TOPIC_COUNT = 5`。
- 第 10 行：`MQTT_TOPIC_LEN = 96`。
- 第 13 行：`SYSTEM_CONFIG_VERSION = 3`。
- 第 31-36 行：`mqtt_server_t` 增加：
  - `topics[5][96]`
  - `qos[5]`
  - `ntp_server[64]`
  - `tls_mode`
  - `tls_verify_peer`
  - `tls_reserved[2]`

文件：`mqttv5_real_board\src\config.c`

- 第 7-11 行：配置区放在 flash 末尾附近：
  - 当前配置：`FLASH_END - 5 * 0x2000`
  - 旧兼容配置地址宏：`FLASH_END - 2 * 0x2000`
- 第 16-38 行：默认配置：
  - DHCP 模式。
  - 静态兜底 IP `192.168.0.30`。
  - MQTT server `192.168.0.110:1883`。
  - device_id `GM400`。
  - 五主题：
    - `city/{city_id}/pole/{pole_id}/device/{device_name}/`
    - `city/{city_id}/pole/{pole_id}/device/{device_name}/get`
    - `city/{city_id}/pole/{pole_id}/device/{device_name}/event`
    - `city/{city_id}/pole/{pole_id}/device/{device_id}/ota`
    - `city/{city_id}/pole/{pole_id}/device/{device_name}/debug`
  - QoS 默认全为 2。
  - NTP 默认 `pool.ntp.org`。
  - TLS 默认关闭 `tls_mode=0`。
- 第 46-62 行：配置有效性校验增加：
  - port 非 0。
  - host 非空。
  - topic[0] 非空。
  - tls_mode 不大于 2。
  - CRC 校验。

## 6. 上位机配置读写/扫描变更

文件：`mqttv5_tool\mqttv5_tool\Management.cs`

- 第 17-20 行：配置块大小和数组长度：
  - `ConfigSizeBytes = 768`
  - `TopicCount = 5`
  - `TopicLength = 96`
  - `NtpServerLength = 64`
- 第 25-26 行：扫描超时和默认板卡网段：
  - `ScanConnectTimeoutMs = 800`
  - `DefaultBoardScanPrefix = "192.168.0."`
- 第 27-40 行：Modbus 配置块偏移：
  - device id：26。
  - host：58。
  - port：122。
  - username：124。
  - password：156。
  - topics：188。
  - qos：668。
  - ntp：673。
  - tls_mode：737。
  - tls_verify_peer：738。
- 第 97-116 行：`Scan()` 自动扫描本机网段，并固定追加 `192.168.0.*`。
- 第 235-258 行：`ToConfigBytes()` 按固件结构打包配置。
- 第 260-283 行：`FromConfigBytes()` 按固件结构解析配置。
- 第 285-295 行：默认五主题与固件 `config.c` 对齐。
- 第 297-327 行：`ScanIpAsync()` 扫到 502 后立即读配置，读通才加入设备列表。
- 第 329-349 行：`GetLocalIpv4Prefixes()` 获取本机 IPv4 网段，跳过 loopback 和 169.254，并补 `192.168.0.`。
- 第 352-383 行：`BuildScanDiagnostics()` 生成扫描失败提示：
  - 列出扫描网段。
  - 提示是否已在 `192.168.0.x`。
  - 对 169.254 自动地址给出直连配置建议。
- 第 406-421 行：大块配置读写拆分成最多 120 个寄存器一组，避免 Modbus 单帧过长。

文件：`mqttv5_tool\mqttv5_tool\Form1.cs`

- 第 223-224 行：查找 0 个设备时，把 `Management.LastScanDiagnostics` 显示在界面上。

## 7. OTA 变更

文件：`mqttv5_real_board\src\real_board_ota.h`

- 第 7-16 行：定义 OTA 分区：
  - flash end：`0x00080000`
  - APP1：`0x00004000`
  - APP2：`0x00042000-0x00076000`
  - 升级状态：`0x0007A000`
  - 最大 chunks：128
  - 最大 chunk：1024
- 第 18-35 行：定义旧 bootloader 可读的升级状态结构：
  - 文件长度。
  - 总包数。
  - 单包长度。
  - 文件 CRC。
  - 升级状态 `0xA55A`。
  - 包接收标志 `pack_flag[128]`。

文件：`mqttv5_real_board\src\real_board_ota.c`

- 第 226-283 行：`ota_begin`：
  - 校验 session_id/file_len/file_crc32/chunk_size/chunk_count。
  - 校验 APP2 容量。
  - 擦除 APP2 区。
  - 初始化 OTA 状态。
- 第 285-353 行：`ota_chunk`：
  - 校验 session。
  - 校验 index/offset/chunk_crc32/data。
  - Base64 解码 `params.data`。
  - 校验 chunk CRC。
  - 写入 APP2。
  - 设置 `pack_flag[index]`。
- 第 355-397 行：`ota_end`：
  - 校验所有包已接收。
  - 计算 APP2 CRC。
  - 写升级状态到 `0x0007A000`。
  - 请求重启，由 bootloader 切换。
- 第 411-452 行：`real_board_ota_handle_command()` 分发：
  - `ota_begin`
  - `ota_chunk`
  - `ota_end`
  - `ota_abort`
  - `ota_status`

## 8. TLS 双向认证变更

文件：`mqttv5_real_board\src\real_board_tls.h`

- 定义 TLS 模式：
  - `0`：明文。
  - `1`：服务端证书认证。
  - `2`：双向认证。

文件：`mqttv5_real_board\src\real_board_tls.c`

- 第 5-15 行：弱符号证书占位：
  - `REAL_BOARD_TLS_CA_CERT_PEM`
  - `REAL_BOARD_TLS_CLIENT_CERT_PEM`
  - `REAL_BOARD_TLS_CLIENT_KEY_PEM`
- 第 21-36 行：`real_board_tls_effective_mode()`：
  - 明确配置优先。
  - 如果端口是 8883 且没有配置 TLS，则推断为服务端认证。
- 第 43-59 行：`parse_ca()` 加载 CA。
- 第 61-90 行：`parse_client_identity()` 加载客户端证书和私钥。
- 第 92-144 行：`real_board_tls_setup()`：
  - TLS1.2。
  - 配置 RNG。
  - 可关闭 peer verify。
  - 设置 hostname。
  - `tls_mode=2` 时配置 client cert/key。

文件：`mqttv5_real_board\src\main.c`

- 第 222-229 行：TLS 上下文。
- 第 236-301 行：`tls_handshake()` 调用 `real_board_tls_setup()` 后执行握手。
- 第 315-366 行：`mqtt_send/mqtt_recv` 根据 `mqtt_tls_active` 选择 SSL 或 W5500 明文 socket。
- 第 370-391 行：`mqtt_init()` 根据配置决定是否 TLS。

## 9. NTP 状态机变更

文件：`mqttv5_real_board\src\real_board_ntp.c`

- 第 11-15 行：NTP 参数：
  - socket 7。
  - 中国时区常量。
  - poll divider。
  - timeout。
  - 1 小时重同步。
- 第 17-36 行：NTP 状态：
  - idle。
  - resolving。
  - syncing。
  - synced。
  - error。
- 第 41-91 行：解析 IPv4 文本。
- 第 99-110 行：`start_sync()` 初始化 SNTP。
- 第 112-137 行：`resolve_server()` 支持 IP 和域名。
- 第 145-184 行：`real_board_ntp_task()`：
  - 周期触发。
  - 域名解析。
  - SNTP run。
  - 成功后记录 unix time。
  - 超时转 error。
  - synced 后按周期重同步。
- 第 191-211 行：`real_board_ntp_append_status()` 把 NTP 状态加入 `get_status`。

文件：`mqttv5_real_board\src\main.c`

- 第 1117 行：启动时 `real_board_ntp_init()`。
- 第 1136 行：主循环调用 `real_board_ntp_task()`。
- 第 734 行：`get_status` 返回 NTP 状态。

## 10. W5500/Modbus 变更

文件：`mqttv5_real_board\src\task_w5500.c`

- 第 12-22 行：保留两个 socket：
  - socket0：MQTT client。
  - socket1：Modbus TCP server 502。
- 第 30-64 行：`parse_ipv4_address()` 支持 MQTT host 是纯 IP 时不走 DNS。
- 第 66-85 行：`w5500_apply_mqtt_server_config()` 从 `system_config` 应用 MQTT host/port。
- 第 87-138 行：`w5500_tcp_server()` 管理 socket1：
  - `SOCK_ESTABLISHED`：等待 Modbus 层读。
  - `SOCK_CLOSE_WAIT`：disconnect + close。
  - `SOCK_FIN_WAIT/SOCK_CLOSING/SOCK_TIME_WAIT/SOCK_LAST_ACK`：强制 close。
  - `SOCK_INIT`：listen。
  - `SOCK_CLOSED`：socket 502。
- 第 257-270 行：`w5500_init()` 从配置结构载入网络信息。
- 第 273-290 行：`w5500_task()`：
  - 获取 PHY link。
  - 首次或掉线时执行 `network_init()`。
  - 应用 MQTT server 配置。
  - 跑 MQTT client 和 Modbus server。

文件：`mqttv5_real_board\src\main.c`

- 第 1071-1093 行：`modbus_task()`：
  - 处理 NanoModbus。
  - coil1 触发保存/重启。
  - 上位机写配置后调用 `config_save()`。
- 第 1126-1131 行：
  - MQTT socket 和 Modbus socket 各分配 2048 buffer。
  - 把 `system_config_temp` 拷贝到 Modbus holding registers。
- 第 1139-1140 行：
  - socket1 established 时才执行 `modbus_task()`。

## 11. 测试和构建变更

文件：`开发环境\firmware_tests\test_real_board_business_contract.py`

- 第 57-81 行：新增/强化旧业务帧契约测试：
  - 检查 A1-A5 宏。
  - 检查 MQTT 入口调用 `real_board_business_handle_legacy_command`。
  - 检查 `legacy_frame`。
  - 检查 `frame` 字段。
  - 检查 Base64 decode/encode。
  - 检查 CRC。
  - 检查 `FE A5`。
  - 检查新修正的 `REAL_BOARD_LEGACY_SYNC_RESPONSE_CMD 0x81U`。
  - 检查 `data.msg` 旧别名。
  - 检查 legacy response code 映射逻辑。

最近验证结果：

- 静态契约测试：
  - 命令：`python -m unittest test_local_emqx_firmware_config.py test_real_board_business_contract.py test_net48_ui_contract.py`
  - 结果：`Ran 37 tests OK`
- 真实板卡固件构建：
  - 命令：`Build-RealBoard-Firmware.ps1 -Clean`
  - 结果：构建成功。
  - HEX 范围：`0x00004000-0x0002C1DF`
- 安全烧录：
  - 镜像：`mqttv5_real_board\eide\build\LocalEMQX\mqttv5_real_board.hex`
  - 结果：`APP verify passed at 0x00004000; bootloader region 0x00000000-0x00003FFF unchanged.`
- 板卡 Modbus：
  - IP：`192.168.0.30`
  - TCP 502 读取 holding registers 连续两次成功。
  - 返回 29 字节，包含配置块开头数据。

## 12. 当前仍未完全满足的需求/风险

### 12.1 真实传感器采样未接底层驱动

当前状态：

- `real_board_business_update_meter_hlw8112()` 已有。
- `real_board_business_update_environment()` 已有。
- `real_board_business_update_rs485()` 已有。
- telemetry JSON 字段已完整。

缺口：

- 没有在主循环调用上述 update 函数。
- 当前工程未接入真实 HLW8112 采样驱动。
- 当前工程未接入真实环境传感器驱动。
- 当前工程未接入真实 RS485 多设备轮询协议。
- 因此实际上报中这些字段会是默认值或 `valid=false`，除非后续外部驱动主动调用这些接口。

影响：

- 协议和字段兼容已经具备。
- 真实业务数据还没有闭环。

### 12.2 A5 外设透传还没有真实外设发送队列

当前状态：

- MQTT 下发 A5 能进入 pending 队列。
- 有 `real_board_business_handle_peripheral_response()` 接收真实外设响应的入口。

缺口：

- A5 请求还没有实际发到 UART/RS485/外设总线。
- 多通道外设队列只恢复了上层 pending/状态承载，没有恢复旧工程完整外设通信链路。

影响：

- 服务器能验证下发被板卡接受。
- 不能证明真实外设已执行命令。

### 12.3 A1/A2 只实现了旧工程中可确认的一部分

当前实现：

- A1：`0x09/0x0A/0x0B/0x0C/0xFA/0xFB`。
- A2：`0x01/0x02/0x03/0x0D/0x0E`。
- 未知命令返回 received，不再报错。

缺口：

- `资料\user\user\protocol.h` 中还定义了 `0x01-0x08` 等 A1 普通参数命令，但旧 `Protocol_Factory_Set()` 实际重点处理 `0x09/0x0A/0x0B/0x0C/0xFA/0xFB`。
- 如果服务器后续真的下发 A1 `0x01-0x08` 并要求旧 payload 精确响应，需要继续补 payload 级协议。

影响：

- “旧业务帧可接收、不报错、可回 received”满足兼容验证。
- “旧全部命令语义 1:1 恢复”未完全满足。

### 12.4 上报仍是新 JSON 结构，不是完全旧阿里云 JSON

当前状态：

- 新 telemetry 使用 `emqx-gateway.realboard.telemetry.v1`。
- 旧业务下发/响应兼容 Base64 帧。
- 旧同步响应带 `data.msg`。

缺口：

- 自动 telemetry 没有完全恢复为旧阿里云：
  - `{"id":"123","version":"1.0","params":{"params":"base64"},"method":"thing.event.property.post"}`
- 如果服务器只接受旧阿里云 property.post 外壳，需要增加“旧上报镜像”或切换 telemetry 生成方式。

影响：

- 当前更适合 EMQX/MQTT5 新服务端。
- 若客户服务端短期只按旧阿里云 payload 解析，需要再补一个 legacy telemetry publish。

### 12.5 OTA 已有协议和写入逻辑，但未做真实升级切换闭环验证

当前状态：

- 支持 `ota_begin/ota_chunk/ota_end/ota_abort/ota_status`。
- APP2 写入、分片 CRC、整包 CRC、升级状态写入已实现。
- 结束后请求重启，由 bootloader 切换。

缺口：

- 没有用真实第二固件包完成现场 OTA 切换验证。
- 没有确认当前 bootloader 对 `0x0007A000` 的结构和 `0xA55A` 状态完全一致。
- 没有验证失败回滚路径。

影响：

- OTA 通信和写 flash 逻辑具备。
- “完整 OTA 升级切换”还需要现场端到端测试。

### 12.6 TLS 双向认证代码有入口，但证书未内置

当前状态：

- `tls_mode=2` 支持双向认证。
- 证书弱符号占位已提供。
- MQTT 连接可走 TLS1.2。

缺口：

- CA/client cert/client key 现在是空字符串。
- 没有客户证书注入文件或构建宏。
- 没有 EMQX 8883 双向认证实测记录。

影响：

- 代码路径具备。
- 真正双向认证上线前必须注入证书并实测。

### 12.7 NTP 状态机有，但未实测外网/局域网 NTP

当前状态：

- NTP 状态机会解析服务器、SNTP 同步、返回 status。

缺口：

- 没有现场 NTP 服务器连通性测试。
- 默认 `pool.ntp.org` 在纯局域网环境可能不可达。

影响：

- 局域网 EMQX 场景下建议配置内网 NTP 或关闭对强同步依赖。

### 12.8 上位机扫描可用，但 TCP 探测方式会占用单连接 socket

当前状态：

- 实测 Modbus 读配置成功。
- 上位机 `ScanIpAsync()` 扫到 502 后会立即读配置，正常情况下能发现设备。

风险：

- W5500 socket1 是单连接 TCP server。
- 只建立 TCP 不发 Modbus，再关闭不及时，可能让下一次纯 TCP 探测短时间失败。

影响：

- 正常上位机扫描应读配置，不只是 TCP connect。
- 如果用户频繁用端口扫描工具扫 502，可能干扰上位机发现。

### 12.9 EMQX 界面联调未在本轮重新完成

当前状态：

- 代码已烧录并通过 Modbus。
- 旧业务帧 JSON 兼容已改。

缺口：

- 本轮没有重新启动并确认 EMQX dashboard 客户端在线。
- 本轮没有抓取 EMQX 收到 telemetry/event/legacy response 的界面截图。

影响：

- 板卡侧和编译烧录侧已验证。
- “EMQX Web 界面完整测试报告”仍需要继续执行。

## 13. 建议的下一步

1. 如果服务器短期按旧阿里云解析，优先补 `legacy telemetry publish`：周期上报 `{"id":"123","version":"1.0","params":{"params":"base64"},"method":"thing.event.property.post"}`，其中 base64 是旧业务帧。
2. 接入真实 HLW8112/环境/RS485 驱动，在主循环或定时任务中调用三个 `real_board_business_update_*()`。
3. 明确 A1 `0x01-0x08` 是否还会被服务器使用；如果会，按旧 payload 补齐响应。
4. 用真实第二固件包做 OTA 切换闭环测试。
5. 注入客户证书后做 EMQX 8883 双向认证测试。
6. 重新跑上位机界面截图测试和 EMQX dashboard 截图测试，补最终测试报告。
