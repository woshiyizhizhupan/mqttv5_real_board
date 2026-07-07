# 当前 EMQX 业务合同与客户工具说明

日期：2026-07-02

## 1. 当前默认连接参数

- Broker：`39.103.154.108`
- MQTT 端口：`1883`
- 默认设备/账号：`GM400-452089`
- 默认密码：`public`
- 当前上位机不启用 TLS；MQTT TLS 仅作为固件/EMQX 后续安全接入能力保留。

## 2. 当前主题合同

当前版本采用服务端确认的单主题业务格式：

| 方向 | 主题 | 说明 |
| --- | --- | --- |
| 服务端 -> 设备 | `v1/devices/request/{device_name}` | 下行控制、查询、OTA 都走该主题 |
| 设备 -> 服务端 | `v1/devices/response/{device_name}` | 设备响应、业务上报、OTA 回包都可走该主题 |
| 设备状态 | `v1/devices/status/{device_id}` | 设备上线/离线状态 |
| 服务端状态订阅 | `v1/devices/status/+` | 订阅所有设备状态；`+` 只能用于订阅，不能用于发布 |

默认实际主题：

- 下行：`v1/devices/request/GM400-452089`
- 上行：`v1/devices/response/GM400-452089`
- 状态：`v1/devices/status/GM400-452089`

固件内部仍保留五个业务 topic 配置槽，用来兼容旧上位机和旧配置结构。当前默认映射为：

- topic1 `telemetry_up` -> `v1/devices/response/{device_name}`
- topic2 `cmd_down` -> `v1/devices/request/{device_name}`
- topic3 `event_up` -> `v1/devices/response/{device_name}`
- topic4 `ota_down` -> `v1/devices/request/{device_name}`
- topic5 `debug_up` -> `v1/devices/response/{device_name}`

状态主题不占用五个业务 topic 配置槽，由固件按 `device_id` 自动生成。

## 3. 下行业务报文格式

服务端下发到：

```text
v1/devices/request/{device_name}
```

推荐格式：

```json
{"payload":"FEA501000BFE010003FFFF502EFC45833F1E03D2"}
```

要求：

- `payload` 是旧业务层十六进制帧字符串。
- `connectType`、`msgType`、`timestamp`、`qos`、`retained` 等其他字段允许存在，设备端忽略不影响业务解析。
- 当前兼容 `{"payload":"...hex..."}` 和旧测试工具的 `{"frame":"...base64..."}`。

示例：

```json
{
  "connectType": "1",
  "msgType": "1",
  "payload": "FEA501000BFE010003FFFF502EFC45833F1E03D2",
  "timestamp": "1782916895695"
}
```

## 4. 设备回包格式

设备发布到：

```text
v1/devices/response/{device_name}
```

推荐格式：

```json
{"payload":"FEA581000BFE010003FFFF502EFC45833F1E03D2"}
```

当前模拟设备端和上位机调试客户端会把该类报文按旧业务帧解析，展示：

- 原始 JSON
- `payload` 原始十六进制
- 旧业务帧命令字
- 业务载荷
- CRC 解析状态
- OTA 阶段含义

## 5. 状态/LWT 逻辑

设备 CONNECT 时登记遗嘱消息：

```text
topic: v1/devices/status/{device_id}
payload: {"schema":"emqx-gateway.status.v1","device_id":"GM400-452089","status":"offline","reason":"mqtt_lwt"}
retain: true
qos: 0
```

设备 MQTT 连接成功后主动发布上线：

```text
topic: v1/devices/status/{device_id}
payload: {"schema":"emqx-gateway.status.v1","device_id":"GM400-452089","status":"online","reason":"mqtt_connected"}
retain: true
qos: 0
```

服务端只需要订阅：

```text
v1/devices/status/+
```

## 6. 工具说明

### 上位机

路径：

```text
03_host_tool\net48\mqttv5_tool.exe
```

功能：

- 局域网扫描板卡，五线程并发，显示进度和预计剩余时间。
- 读取/写入板卡 MQTT 参数、五个业务主题、NTP、TLS 参数。
- 内置 EMQX 客户端，支持状态/LWT 独立分页。
- 报文表格默认不自动抢选中行；需要跟随最新时手动勾选“自动跟随最新报文”。
- 含原始日志、格式化 JSON、字段含义和数据含义。

### 独立 EMQX 模拟设备端

路径：

```text
01_emqx_gui_client\emqx_device_emulator_csharp\EmqxDeviceEmulator.App.exe
```

功能：

- 模拟设备订阅 `v1/devices/request/GM400-452089`。
- 收到 `payload` 十六进制旧业务帧后解析并回包到 `v1/devices/response/GM400-452089`。
- 支持旧 OTA B0/B1/B2/B3/B4/B5/B6 流程。
- CONNECT 时登记 LWT，连接成功后发布 retained online 状态。
- 可查看下行/get、OTA、响应上报、状态/LWT、原始日志和报文详情。

## 7. 本次验证

- 上位机 Release 构建：通过，0 warning / 0 error。
- 独立 EMQX 模拟设备端 Release 构建：通过，0 warning / 0 error。
- 协议测试：8 项通过，覆盖 `payload` 十六进制旧业务帧优先解析、Base64 envelope、OTA 512 分包。
- 固件/上位机契约测试：107 项通过。
- 固件 APP 链接区保持从 `0x00004000` 开始，未改变 bootloader 区域约束。
