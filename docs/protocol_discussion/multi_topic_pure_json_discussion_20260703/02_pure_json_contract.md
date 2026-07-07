# 纯 JSON 协议合同草案

## 设计原则

- 每个 MQTT payload 都是 JSON object。
- 所有上行业务包都带 `schema`、`device_id`、`firmware_version` 或可追踪版本字段。
- 服务端下行命令统一使用 `cmd + params`，不要再要求业务软件拼固定十六进制帧。
- 字段名使用小写蛇形命名，保持当前固件 JSON 风格。
- 数值带单位，例如 `voltage_mv`、`temperature_c_x10`，避免浮点。
- 旧 `payload` 十六进制帧保留为兼容模式，不作为新业务主路径。

## 公共字段

| 字段 | 类型 | 方向 | 说明 |
| --- | --- | --- | --- |
| `schema` | string | 双向 | 协议名和版本 |
| `id` | string | 下行/响应 | 服务端请求 ID，响应原样带回 |
| `device_id` | string | 双向 | 设备 ID，例如 `GM400-452089` |
| `cmd` | string | 下行/响应 | 命令名 |
| `params` | object | 下行 | 命令参数 |
| `ok` | bool | 上行响应 | 是否成功 |
| `code` | number | 上行响应 | 0 成功，400/404/422 等失败 |
| `message` | string | 上行响应 | 人可读说明 |
| `data` | object | 上行响应/事件 | 响应数据或事件数据 |
| `seq` | number | 上行可选 | 设备端递增序号，建议后续增加 |
| `uptime_ticks` | number | 上行 | 当前已有业务 tick |

## 下行命令

topic：

```text
v1/devices/request/{device_id}
```

格式：

```json
{
  "schema": "emqx-gateway.command.v1",
  "id": "srv-20260703-0001",
  "device_id": "GM400-452089",
  "cmd": "set_lighting",
  "params": {
    "lamp1_brightness": 80,
    "lamp2_brightness": 40,
    "relay1_on": true,
    "relay2_on": false
  }
}
```

建议服务端命令名：

| 命令 | 当前固件基础 | 说明 |
| --- | --- | --- |
| `ping` | 已有 | 连通性测试 |
| `get_status` | 已有 | 读取状态、业务摘要、OTA/NTP 状态 |
| `get_config` | 已有 | 读取 MQTT/网络配置，不返回明文密码 |
| `set_config` | 已有 | 写配置，可要求重启 |
| `reboot` | 已有 | 重启 |
| `set_lighting` | 建议新增别名 | 设置灯亮度和继电器，内部可复用 `real_set` |
| `queue_event` | 建议新增别名 | 测试或人工触发事件，内部可复用 `real_event` |
| `legacy_frame` | 已有兼容 | 旧 A1/A2/A5 十六进制或 Base64 帧，仅过渡期使用 |

当前内部命令 `real_set`、`real_event` 可以继续保留，但建议服务端文档暴露更业务化的 `set_lighting`、`queue_event`。

## OTA 下行

topic：

```text
v1/devices/ota/{device_id}
```

命令仍使用 JSON：

```json
{
  "schema": "emqx-gateway.command.v1",
  "id": "ota-0001",
  "device_id": "GM400-452089",
  "cmd": "ota_begin",
  "params": {
    "file_len": 196608,
    "chunk_count": 384,
    "chunk_size": 512,
    "file_crc32": "0x12345678"
  }
}
```

OTA 分包：

```json
{
  "schema": "emqx-gateway.command.v1",
  "id": "ota-0002",
  "device_id": "GM400-452089",
  "cmd": "ota_chunk",
  "params": {
    "index": 0,
    "data_b64": "BASE64_512_BYTES"
  }
}
```

说明：

- `data_b64` 用 Base64 表示二进制分包。
- `ota_status`、`ota_end`、`ota_abort` 同样用 `cmd` 区分。
- 如果当前固件 OTA JSON 还未支持 `file_len/chunk_count/data_b64` 这些参数，需要按实现计划补齐；旧 B0-B6 仍可兼容。

## 响应

topic：

```text
v1/devices/event/{device_id}
```

格式：

```json
{
  "schema": "emqx-gateway.response.v1",
  "id": "srv-20260703-0001",
  "device_id": "GM400-452089",
  "cmd": "set_lighting",
  "ok": true,
  "code": 0,
  "message": "ok",
  "data": {
    "lamp1_brightness": 80,
    "lamp2_brightness": 40,
    "relay1_on": true,
    "relay2_on": false
  }
}
```

错误示例：

```json
{
  "schema": "emqx-gateway.response.v1",
  "id": "srv-20260703-0002",
  "device_id": "GM400-452089",
  "cmd": "set_lighting",
  "ok": false,
  "code": 422,
  "message": "lamp1_brightness must be 0-100"
}
```

## 遥测

topic：

```text
v1/devices/telemetry/{device_id}
```

当前固件已生成：

```json
{
  "schema": "emqx-gateway.realboard.telemetry.v1",
  "device_id": "GM400-452089",
  "firmware_version": "real-board-app1",
  "business_mode": "real_board",
  "meter_hlw8112": {
    "valid": true,
    "source": "real_driver",
    "voltage_mv": 220000,
    "current_ma": 1200,
    "active_power_mw": 260000,
    "reactive_power_mvar": 0,
    "power_factor_x1000": 980,
    "energy_one_wh": 12,
    "energy_total_wh": 1024,
    "pulse_count": 10
  },
  "environment": {
    "valid": true,
    "source": "real_driver",
    "temperature_c_x10": 256,
    "humidity_rh_x10": 635,
    "pm25_ugm3": 18,
    "co2_ppm": 520,
    "illuminance_lux": 300
  },
  "rs485": {
    "valid": true,
    "online": true,
    "device_count": 1,
    "read_period_s": 30,
    "last_response_ms": 32,
    "tx_count": 100,
    "rx_count": 99,
    "error_count": 1
  },
  "lighting": {
    "lamp1_brightness": 80,
    "lamp2_brightness": 40,
    "relay1_on": true,
    "relay2_on": false,
    "open_lamp_seconds": 3600,
    "bright_time_seconds": 300,
    "bright_time_total_seconds": 7200
  },
  "device": {
    "address": 1,
    "keepalive_s": 60,
    "temperature_c_x10": 260,
    "error_code": 0,
    "error_renew": false,
    "uptime_ticks": 123456
  }
}
```

建议新增但不强制第一版实现：

- `seq`：设备上行递增序号。
- `time`：如果 NTP 已同步，可放 `yyyy-mm-dd hh:mm:ss` 或秒级时间。

## 事件/告警

topic：

```text
v1/devices/event/{device_id}
```

格式：

```json
{
  "schema": "emqx-gateway.realboard.event.v1",
  "device_id": "GM400-452089",
  "firmware_version": "real-board-app1",
  "business_mode": "real_board",
  "event": "rs485_offline",
  "level": "warn",
  "uptime_ticks": 123456,
  "data": {
    "meter_hlw8112_valid": true,
    "environment_valid": true,
    "rs485_valid": false,
    "rs485_online": false,
    "error_code": 12
  }
}
```

建议事件名：

| event | level | 说明 |
| --- | --- | --- |
| `boot` | info | 设备启动 |
| `config_saved` | info | 配置写入成功 |
| `rebooting` | info | 准备重启 |
| `rs485_offline` | warn | RS485 外设离线 |
| `meter_invalid` | warn | 电表数据无效 |
| `environment_invalid` | warn | 环境传感无效 |
| `fault` | error | 设备故障 |
| `fault_recovered` | info | 故障恢复 |

## 调试

topic：

```text
v1/devices/debug/{device_id}
```

格式：

```json
{
  "schema": "emqx-gateway.debug.v1",
  "device_id": "GM400-452089",
  "level": "warn",
  "message": "cmd/down payload is not valid JSON",
  "reason": "invalid_json"
}
```

调试 topic 不建议服务端长期入业务库，只用于联调、灰度和问题定位。

## 状态/LWT

topic：

```text
v1/devices/status/{device_id}
```

上线：

```json
{
  "schema": "emqx-gateway.status.v1",
  "device_id": "GM400-452089",
  "status": "online",
  "reason": "mqtt_connected"
}
```

异常离线 LWT：

```json
{
  "schema": "emqx-gateway.status.v1",
  "device_id": "GM400-452089",
  "status": "offline",
  "reason": "mqtt_lwt"
}
```

状态 topic 保持 retain=true、QoS 0。

## 兼容 payload

过渡期允许：

```json
{
  "payload": "FEA501000BFE010003FFFF502EFC45833F1E03D2"
}
```

但新服务端不应再新增依赖这个格式。建议把这种格式标为 `legacy_frame`：

```json
{
  "schema": "emqx-gateway.command.v1",
  "id": "legacy-0001",
  "device_id": "GM400-452089",
  "cmd": "legacy_frame",
  "params": {
    "payload": "FEA501000BFE010003FFFF502EFC45833F1E03D2",
    "encoding": "hex"
  }
}
```
