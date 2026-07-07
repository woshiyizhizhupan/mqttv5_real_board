# 多主题方案建议

## 目标

把真实固件从“单 request/response 主题 + 固定 payload 帧”为主，升级成“多业务 topic + 纯 JSON”为主，同时保留现有上位机、Modbus 配置块和旧业务帧兼容能力。

## 三个可选方案

### 方案 A：继续单主题，只靠 JSON schema 区分业务

主题保持：

- 下行：`v1/devices/request/{device_id}`
- 上行：`v1/devices/response/{device_id}`
- 状态：`v1/devices/status/{device_id}`

优点：服务端改动小，当前固件最接近。

缺点：遥测、响应、事件、调试、OTA 全混在 response 或 request 中，服务端规则和日志排查会变复杂；设备也容易收到自己发布的兼容回包。

结论：可作为兼容模式，不建议作为真实版本最终方案。

### 方案 B：5 个业务 topic + 独立状态 topic

这是推荐方案。使用当前 5 个 topic 配置槽，不扩结构：

| 槽位 | 固件枚举 | 方向 | 推荐默认 topic | 用途 |
| ---: | --- | --- | --- | --- |
| 0 | `MQTT_TOPIC_TELEMETRY_UP_INDEX` | 上行 | `v1/devices/telemetry/{device_id}` | 周期遥测快照 |
| 1 | `MQTT_TOPIC_CMD_DOWN_INDEX` | 下行 | `v1/devices/request/{device_id}` | 普通命令、查询、配置 |
| 2 | `MQTT_TOPIC_EVENT_UP_INDEX` | 上行 | `v1/devices/event/{device_id}` | 命令响应、事件、告警、OTA ACK |
| 3 | `MQTT_TOPIC_OTA_INDEX` | 下行 | `v1/devices/ota/{device_id}` | OTA begin/chunk/end/status/abort |
| 4 | `MQTT_TOPIC_DEBUG_UP_INDEX` | 上行 | `v1/devices/debug/{device_id}` | 调试、兼容提示、异常 JSON |

独立状态 topic：

```text
v1/devices/status/{device_id}
```

状态 topic 不占 5 个业务槽，由固件按 `device_id` 自动生成。服务端订阅 `v1/devices/status/+`，设备绝不向带 `+` 的通配 topic 发布。

优点：

- 与现有 `mqtt_server.topics[5][96]` 完全匹配。
- 只需调整默认 topic、发布路由、订阅 OTA topic 和测试契约。
- 服务端可以按 topic 做规则、存储和权限隔离。
- 如果客户短期仍要单主题，把 5 个槽配置回 request/response 重复值即可兼容。

缺点：

- 命令响应和事件共用 `event` topic。如果服务端要求 ack 与 event 分开，需要扩展到 6 个以上业务 topic，并升级配置结构版本。

### 方案 C：完全细分 topic，ack/event/ota-up/ota-down 全拆

示例：

- `v1/devices/telemetry/{device_id}`
- `v1/devices/command/{device_id}`
- `v1/devices/response/{device_id}`
- `v1/devices/event/{device_id}`
- `v1/devices/ota/request/{device_id}`
- `v1/devices/ota/response/{device_id}`
- `v1/devices/debug/{device_id}`
- `v1/devices/status/{device_id}`

优点：语义最清晰。

缺点：当前固件和上位机只有 5 个业务 topic 槽。要做这个方案必须修改 `system_config_t`、Modbus 配置偏移、上位机读写、INI、测试和历史配置迁移，风险明显更高。

结论：服务端明确要求时再做第二阶段，不建议当前版本直接上。

## 推荐默认值

建议真实版本默认写入：

```text
topic1 telemetry_up = v1/devices/telemetry/{device_id}
topic2 cmd_down    = v1/devices/request/{device_id}
topic3 event_up    = v1/devices/event/{device_id}
topic4 ota_down    = v1/devices/ota/{device_id}
topic5 debug_up    = v1/devices/debug/{device_id}
status             = v1/devices/status/{device_id}
```

推荐 QoS：

| 类型 | 推荐 QoS | 说明 |
| --- | ---: | --- |
| telemetry | 0 或 1 | 周期数据可按服务端要求选择，默认建议 1 |
| cmd_down | 1 | 控制命令至少一次送达 |
| event_up | 1 | 响应和告警至少一次上报 |
| ota_down | 1 或 2 | OTA 可按服务端确认选择；当前固件已支持 QoS 2 ACK |
| debug_up | 0 | 调试信息不应阻塞业务 |
| status/LWT | 0 retained | 当前已实现，保持 |

如果服务端暂时不想调整 QoS，可以保持当前 `{2,2,2,2,2}`，只改 topic 路由。

## 发布/订阅规则

设备订阅：

- `topics[1]`：普通命令。
- `topics[3]`：OTA 命令。如果与 `topics[1]` 相同，只订阅一次。

设备发布：

- telemetry JSON 只发 `topics[0]`。
- command response、业务事件、告警、OTA ACK 发 `topics[2]`。
- debug JSON 发 `topics[4]`。
- LWT online/offline 发 `v1/devices/status/{device_id}`，retain=true。

服务端订阅建议：

```text
v1/devices/telemetry/+
v1/devices/event/+
v1/devices/debug/+
v1/devices/status/+
```

服务端发布建议：

```text
v1/devices/request/{device_id}
v1/devices/ota/{device_id}
```

## 兼容策略

兼容旧单主题：

- 上位机仍允许五个 topic 填成同一 request/response。
- 固件保留“收到自己发布的旧 `payload` 响应时忽略”的逻辑。
- 服务端未确认前，旧 `{"payload":"FE..."}` 和 `{"frame":"base64"}` 不删除。

纯 JSON 目标：

- 新服务端只发 `{"schema":"emqx-gateway.command.v1","cmd":"...","params":{...}}`。
- 旧固定帧只作为 `legacy_frame` 或 `payload` 兼容入口。
- 文档和新测试默认以纯 JSON 为主。
