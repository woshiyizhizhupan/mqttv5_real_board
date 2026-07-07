# EMQX 模拟设备端客户端

该工具是独立 C# WinForms 上位机，用来模拟板卡 MQTT 设备端。它连接 EMQX，订阅设备下行主题，记录其他 EMQX 客户端实际发来的报文，并按当前固件逻辑模拟响应。

## 默认连接参数

- Broker Host: `39.103.154.108`
- Broker Port: `1883`
- MQTT Version: `5.0`
- TLS: `false`
- QoS: `2`
- 模拟设备 ClientId: `GM400-67890-device-emulator`
- Username: `GM400-67890`
- Password: `public`

第三方客户端也可以使用同一组用户名/密码连接，但 ClientId 必须唯一，例如 `third-party-ota-debugger-001`。

## 默认五个主题

- 遥测上行: `city/zhxm01/pole/zhxm002/device/67890/`
- 下行/get: `city/zhxm01/pole/zhxm002/device/67890/get`
- 事件/响应: `city/zhxm01/pole/zhxm002/device/67890/event`
- OTA下行: `city/zhxm01/pole/zhxm002/device/67890/ota`
- 调试上行: `city/zhxm01/pole/zhxm002/device/67890/debug`

模拟设备端默认订阅 `下行/get` 和 `OTA下行`。如果需要监听对方实际使用的其他 topic，可以在“额外订阅主题”中一行一个添加。

## 给其他 EMQX 客户端的联调信息

其他客户端需要发布到：

- 普通命令发布到 `city/zhxm01/pole/zhxm002/device/67890/get`
- OTA命令发布到 `city/zhxm01/pole/zhxm002/device/67890/ota`

其他客户端需要订阅响应：

- `city/zhxm01/pole/zhxm002/device/67890/event`

最小 ping 下发：

```json
{"id":"third-ping-001","cmd":"ping"}
```

模拟设备响应：

```json
{"schema":"emqx-gateway.response.v1","id":"third-ping-001","cmd":"ping","ok":true,"code":0,"message":"ok","device_id":"GM400-67890","firmware_version":"csharp-device-emulator-20260701","data":{"pong":true,"device_id":"GM400-67890"}}
```

## EMQX 消息外壳兼容

工具同时兼容部分 EMQX/第三方客户端导出的消息外壳格式，真实业务内容放在 `payload` 字段中，并按 Base64 编码：

```json
{
  "mutable": true,
  "payload": "SGVsbG8gV29ybGQ=",
  "qos": 1,
  "retained": false,
  "dup": false,
  "messageId": 12345,
  "properties": {
    "property1": "value1",
    "property2": 42
  }
}
```

处理规则：

- 如果 `payload` 解码后是普通文本，例如 `Hello World`，工具会按 `mqtt_envelope` 处理并回复 `ok=true`，响应里带 `payload_text` 和 `payload_hex`。
- 如果 `payload` 解码后是 JSON 命令，例如 `{"id":"inner-ping","cmd":"ping"}`，工具会继续执行内层 `cmd`，响应使用内层 `id/cmd`。
- 外层 `qos`、`retained`、`dup`、`messageId` 会记录在解析详情和响应 `data` 中，方便对方客户端核对。

## Legacy OTA 报文约定

OTA 下发使用 JSON 外壳套旧业务帧：

```json
{"id":"legacy-b1-001","cmd":"legacy_frame","frame":"<base64 legacy frame>"}
```

下发帧结构：

- 外层: `FE A5 01 <inner_len:u16_be> <inner_frame> <crc32_be>`
- 内层: `FE <cmd:B0-B4> <payload_len:u16_be> <payload> <crc32_be>`
- CRC32 覆盖各自帧头 `FE` 之后到 CRC 之前的数据。

B0-B4:

- `B0`: payload 空，查询升级信息。
- `B1`: 文件信息。推荐 payload 为 `file_len:u32_be + chunk_size:u16_be + chunk_count:u16_be + file_crc32:u32_be`。工具也兼容 `file_len + chunk_count + chunk_size + crc32`。
- `B2`: 分包数据。payload 为 `chunk_index:u16_be + chunk_data`，当前测试按 `512` 字节分包，最后一包可小于 512。
- `B3`: payload 空，检查缺包和整包 CRC。成功响应载荷为 `FF FF 01`。
- `B4`: payload 空，结束升级并模拟切换。成功响应载荷为 `01`。

响应 JSON 的 `data.frame` 是 Base64 编码响应帧：

- 响应帧: `FE A5 <cmd:B0-B4> <payload_len:u16_be> <payload> <crc32_be>`

## 使用注意

如果真实板卡也使用同一组 topic 在线，那么第三方客户端会同时收到真实板卡和模拟设备端的响应。要单独观察第三方下发报文，建议临时断开真实板卡，或让第三方使用一组专门用于模拟的 device topic。

日志目录在 exe 同级 `logs` 目录中：

- `device_emulator_yyyyMMdd.log`: 人类可读日志。
- `device_emulator_yyyyMMdd.jsonl`: 每条 RX/TX 的原始报文、hex 和解析结果。
