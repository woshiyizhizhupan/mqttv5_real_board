# 固件代码逻辑建议

## 当前代码状态

真实板固件已经具备多主题和 JSON 改造基础：

- `mqttv5_real_board/src/config.h`
  - `MQTT_TOPIC_COUNT = 5`
  - `topics[5][96]`
  - `qos[5]`
  - `tls_mode` / `tls_verify_peer`
- `mqttv5_real_board/src/config.c`
  - 当前默认仍把 topic 0/2/4 填成 `v1/devices/response/{device_id}`。
  - topic 1/3 填成 `v1/devices/request/{device_id}`。
- `mqttv5_real_board/src/main.c`
  - 已定义 `MQTT_TOPIC_TELEMETRY_UP_INDEX`、`MQTT_TOPIC_CMD_DOWN_INDEX`、`MQTT_TOPIC_EVENT_UP_INDEX`、`MQTT_TOPIC_OTA_INDEX`、`MQTT_TOPIC_DEBUG_UP_INDEX`。
  - 已实现 `mqtt_publish_json_to_topic_ex(topic, json, qos, retain)`。
  - 当前多数上行仍通过 `mqtt_publish_response_json()` 发到 topic 0。
  - 当前只订阅 `mqtt_get_request_topic()`，未单独订阅 topic 3 OTA。
  - 状态/LWT 已经使用 `v1/devices/status/{device_id}`。
- `mqttv5_real_board/src/real_board_business.c`
  - 已实现 telemetry/event/status JSON 构造。
  - 已保留旧 A1/A2/A5 业务帧解析。

## 必要改动

### 1. 默认 topic 从 request/response 改成多主题

建议在 `config.c` 增加前缀：

```c
#define REAL_BOARD_DEFAULT_TELEMETRY_TOPIC_PREFIX "v1/devices/telemetry/"
#define REAL_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX "v1/devices/request/"
#define REAL_BOARD_DEFAULT_EVENT_TOPIC_PREFIX "v1/devices/event/"
#define REAL_BOARD_DEFAULT_OTA_TOPIC_PREFIX "v1/devices/ota/"
#define REAL_BOARD_DEFAULT_DEBUG_TOPIC_PREFIX "v1/devices/debug/"
```

`config_reset()` 生成：

```text
topics[0] = telemetry prefix + device_id
topics[1] = request prefix + device_id
topics[2] = event prefix + device_id
topics[3] = ota prefix + device_id
topics[4] = debug prefix + device_id
```

是否升级 `SYSTEM_CONFIG_VERSION`：

- 如果希望老设备刷固件后自动换成新默认 topic，必须升版本，例如 3 -> 4。
- 如果希望保留现场已经写入的 topic，不升版本，由上位机或服务端批量配置写入新 topic。

建议真实客户版本：先不强制清现场配置，提供上位机一键写入；量产默认固件再升版本。

### 2. 增加按槽位发布的 helper

当前 `mqtt_publish_response_json()` 总是走 telemetry 槽位对应的 response topic。建议改成：

```c
static const char *mqtt_get_configured_topic(mqtt_topic_index_t index)
{
    if ((index < MQTT_TOPIC_COUNT) && (system_config->mqtt_server.topics[index][0] != '\0'))
        return system_config->mqtt_server.topics[index];
    return mqtt_get_response_topic();
}

static int mqtt_publish_index_json(mqtt_topic_index_t index, const char *json)
{
    return mqtt_publish_json_to_topic(
        mqtt_get_configured_topic(index),
        json,
        system_config->mqtt_server.qos[index]);
}
```

然后调整：

- `mqtt_publish_telemetry()` -> `MQTT_TOPIC_TELEMETRY_UP_INDEX`
- `mqtt_publish_real_event()` -> `MQTT_TOPIC_EVENT_UP_INDEX`
- `mqtt_publish_response()` -> `MQTT_TOPIC_EVENT_UP_INDEX`
- `mqtt_publish_debug()` -> `MQTT_TOPIC_DEBUG_UP_INDEX`
- `mqtt_publish_legacy_payload_envelope_response()` -> `MQTT_TOPIC_EVENT_UP_INDEX`

`mqtt_publish_lifecycle_status()` 保持独立 status topic。

### 3. 同时订阅普通命令和 OTA topic

`mqtt_connect()` 成功后：

```c
mqtt_subscribe((char *)mqtt_get_configured_topic(MQTT_TOPIC_CMD_DOWN_INDEX),
               system_config->mqtt_server.qos[MQTT_TOPIC_CMD_DOWN_INDEX]);

if (strcmp(mqtt_get_configured_topic(MQTT_TOPIC_OTA_INDEX),
           mqtt_get_configured_topic(MQTT_TOPIC_CMD_DOWN_INDEX)) != 0)
{
    mqtt_subscribe((char *)mqtt_get_configured_topic(MQTT_TOPIC_OTA_INDEX),
                   system_config->mqtt_server.qos[MQTT_TOPIC_OTA_INDEX]);
}
```

处理下行时继续用：

```c
uint8_t received_on_ota_topic = mqtt_received_topic_matches(MQTT_TOPIC_OTA_INDEX);
```

如果收到的 topic 既不是 cmd 也不是 ota，可以发 debug 后忽略。

### 4. 纯 JSON 命令别名

当前已支持：

- `ping`
- `get_status`
- `get_config`
- `set_config`
- `reboot`
- `real_set`
- `real_event`
- `ota_begin`
- `ota_chunk`
- `ota_end`
- `ota_abort`
- `ota_status`

建议新增服务端更容易理解的别名：

| 新命令 | 复用旧命令逻辑 |
| --- | --- |
| `set_lighting` | `real_set` |
| `queue_event` | `real_event` |

实现方式：

```c
else if ((strcmp(cmd, "set_lighting") == 0) ||
         (strcmp(cmd, "real_set") == 0) ||
         (strcmp(cmd, "sim_set") == 0))
{
    if (real_board_business_handle_json_command(command_root, "real_set", ...))
        ...
}
```

也可以在 `real_board_business_handle_json_command()` 内部支持这些别名。

### 5. Legacy 兼容保留但降级为兼容模式

短期保留：

- `payload` 十六进制。
- Base64 frame。
- `legacy_frame`。
- A1/A2/A5/B0-B6 OTA 旧帧。

文档和测试改成：

- 新 JSON 命令是主路径。
- 旧 fixed payload 是兼容路径。
- 后续可通过 `REAL_BOARD_ENABLE_LEGACY_FRAME 0` 编译开关关闭。

## 上位机影响

上位机当前已支持 5 个 topic 和 QoS，不需要改 Modbus 配置偏移。

建议改：

- `Management.BuildDefaultTopics()` 默认值改成多主题。
- `Form2` / `AdvancedSettingsForm` 标签保留“上行/遥测、下行/控制、事件/告警、OTA在线升级、调试/兼容”。
- MQTT 客户端订阅候选自动显示：
  - 五个业务 topic。
  - `v1/devices/status/+`。
- 增加“一键多主题默认值”或把现有默认一键写入切到多主题。

## 测试影响

需要新增或更新静态契约测试：

- 默认 topic 前缀变成 telemetry/request/event/ota/debug。
- `mqtt_publish_telemetry()` 不再通过 `mqtt_publish_response_json()`。
- `mqtt_publish_response()` 和 `mqtt_publish_real_event()` 发布到 event 槽。
- `mqtt_publish_debug()` 发布到 debug 槽。
- `mqtt_connect()` 订阅 cmd 和 ota 两个 topic，且相同时只订阅一次。
- 状态/LWT 仍不占 5 个业务 topic。
- 上位机默认 topic 与固件一致。

建议运行：

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester\.venv\Scripts\python.exe' -m unittest `
  'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py' `
  'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_mqtt_lwt_status_contract.py' `
  'D:\code\唐家湾嵌入式兼职\开发环境\host_tool_tests\test_net48_ui_contract.py'
```

实现后再构建：

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Build-RealBoard-Firmware.ps1' -Clean
dotnet build 'D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\mqttv5_tool.csproj' -v:minimal
```

烧录仍只能使用项目安全脚本，不允许整片擦除或覆盖 `0x00000000-0x00003FFF`。
