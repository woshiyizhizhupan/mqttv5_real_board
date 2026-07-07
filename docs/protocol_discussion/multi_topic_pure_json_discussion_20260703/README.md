# 多主题 + 纯 JSON 讨论包

日期：2026-07-03

适用对象：

- 真实板固件：`mqttv5_real_board`
- 上位机：`mqttv5_tool`
- 服务端/业务软件：EMQX MQTT 5.0 接入侧

## 结论建议

建议采用“5 个业务 topic + 1 个独立状态 topic”的方案：

| 类型 | 方向 | 推荐 topic |
| --- | --- | --- |
| 遥测 | 设备 -> 服务端 | `v1/devices/telemetry/{device_id}` |
| 命令 | 服务端 -> 设备 | `v1/devices/request/{device_id}` |
| 响应/事件 | 设备 -> 服务端 | `v1/devices/event/{device_id}` |
| OTA 下行 | 服务端 -> 设备 | `v1/devices/ota/{device_id}` |
| 调试/兼容 | 设备 -> 服务端 | `v1/devices/debug/{device_id}` |
| 状态/LWT | 设备 -> 服务端 | `v1/devices/status/{device_id}` |

原因：

- 不扩大 `system_config_t`，仍使用当前 5 个 topic 配置槽，降低 Modbus 配置块和上位机改动风险。
- 保留当前服务端风格：`v1/devices/<业务类型>/{device_id}`。
- 保留现有状态/LWT topic，不把 `status/+` 写入设备配置。
- 纯 JSON 从现有 `cmd + params` 入口扩展，不再把真实业务长期绑定在固定十六进制 `payload` 帧上。
- 旧 `payload` 十六进制/Legacy A1/A2/A5 帧保留为过渡兼容模式，等服务端确认纯 JSON 后再关闭。

## 包内容

- `01_multi_topic_scheme.md`：多主题方案、备选方案和推荐理由。
- `02_pure_json_contract.md`：服务端和固件讨论用 JSON 协议合同。
- `03_firmware_code_logic.md`：当前代码依据、代码落点、建议逻辑。
- `04_implementation_plan.md`：后续真实实现任务清单。
- `05_server_review_questions.md`：发给服务端确认的问题清单。
- `examples/`：可直接复制给服务端或 MQTT 工具测试的 JSON 示例。
- `code/proposed_topic_routing_patch.diff`：拟议代码改动片段，不是已应用补丁。
- `references/current_code_map.md`：当前工程关键文件和函数索引。

## 当前工程依据

当前真实固件已经具备这些基础能力：

- `mqttv5_real_board/src/config.h`：5 个 topic 槽、每 topic QoS、TLS/NTP 配置。
- `mqttv5_real_board/src/main.c`：MQTT 5.0、QoS ACK、JSON 命令入口、LWT 状态 topic。
- `mqttv5_real_board/src/real_board_business.c`：真实板 telemetry/event/status JSON 构造和旧业务帧兼容。
- `mqttv5_tool/mqttv5_tool`：上位机已能配置 5 个 topic、QoS、TLS/NTP，并内置 MQTT 客户端。

## 注意

本讨论包没有修改真实固件源码，没有烧录，也没有触碰 bootloader 区域。后续如果要实现，仍必须使用项目指定的安全构建/烧录脚本。
