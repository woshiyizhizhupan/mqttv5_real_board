# 服务端评审问题清单

把这个清单发给服务端/业务软件确认即可。

## 必须确认

1. 多主题默认是否接受以下命名：

```text
v1/devices/telemetry/{device_id}
v1/devices/request/{device_id}
v1/devices/event/{device_id}
v1/devices/ota/{device_id}
v1/devices/debug/{device_id}
v1/devices/status/{device_id}
```

2. `event` topic 同时承载命令响应、事件、告警、OTA ACK 是否可接受？

如果不能接受，需要扩展到 6 个以上业务 topic，并改配置结构和上位机。

3. QoS 默认值希望怎么定？

建议：

```text
telemetry=1, command=1, event=1, ota=1, debug=0, status=0 retained
```

如果服务端要求 OTA QoS 2，固件当前 MQTT 5 ACK 路径已支持。

4. 服务端是否接受纯 JSON 主路径：

```json
{"schema":"emqx-gateway.command.v1","id":"...","device_id":"...","cmd":"set_lighting","params":{}}
```

5. 是否要求命令响应一定回 `response` topic，而不是 `event` topic？

当前推荐把命令响应放 `event` topic，因为 5 个槽位有限，且 event 表示“设备上行业务结果”。

6. OTA 是否接受 `data_b64` 分包？

如果服务端已有固定 OTA JSON 字段名，请给出字段名、分包大小、CRC 表示方式。

7. 时间字段怎么定？

建议第一版不强制，保留 `uptime_ticks`；NTP 稳定后增加 `time` 或 `ts_ms`。

8. 旧 `payload` 十六进制固定帧需要保留多久？

建议：

- 第一版保留兼容。
- 服务端纯 JSON 验收后，下一版通过编译开关关闭或只保留内部调试。

## 建议服务端订阅

```text
v1/devices/telemetry/+
v1/devices/event/+
v1/devices/debug/+
v1/devices/status/+
```

## 建议服务端发布

普通命令：

```text
topic: v1/devices/request/GM400-452089
payload: {"schema":"emqx-gateway.command.v1","id":"1","device_id":"GM400-452089","cmd":"ping","params":{}}
```

OTA：

```text
topic: v1/devices/ota/GM400-452089
payload: {"schema":"emqx-gateway.command.v1","id":"ota-1","device_id":"GM400-452089","cmd":"ota_status","params":{}}
```

## 验收标准

- 设备连接 EMQX 后，`v1/devices/status/{device_id}` 有 retained online。
- 服务端发 `ping` 到 request topic，设备在 event topic 回响应。
- 设备周期 telemetry 只出现在 telemetry topic。
- 服务端发 `set_lighting` 到 request topic，设备控制灯/继电器并在 event topic 回响应。
- 服务端发 OTA 命令到 ota topic，设备在 event topic 回 ACK。
- 非法 JSON 或未知命令出现在 debug/event，不影响 MQTT 连接。
