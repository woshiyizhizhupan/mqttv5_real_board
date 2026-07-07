# 当前代码索引

## 固件配置

- `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\config.h`
  - `DEVICE_ID_LEN = 32`
  - `MQTT_TOPIC_COUNT = 5`
  - `MQTT_TOPIC_LEN = 96`
  - `system_config_t`
  - `mqtt_server_t`

- `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\config.c`
  - `system_config_def`
  - `config_reset()`
  - `config_save()`
  - `config_payload_is_valid()`

## MQTT 主逻辑

- `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\main.c`
  - `MQTT_TOPIC_TELEMETRY_UP_INDEX = 0`
  - `MQTT_TOPIC_CMD_DOWN_INDEX = 1`
  - `MQTT_TOPIC_EVENT_UP_INDEX = 2`
  - `MQTT_TOPIC_OTA_INDEX = 3`
  - `MQTT_TOPIC_DEBUG_UP_INDEX = 4`
  - `mqtt_get_request_topic()`
  - `mqtt_get_response_topic()`
  - `mqtt_get_status_topic()`
  - `mqtt_publish_json_to_topic_ex()`
  - `mqtt_publish_response()`
  - `mqtt_publish_debug()`
  - `mqtt_publish_telemetry()`
  - `mqtt_publish_real_event()`
  - `mqtt_handle_command_json()`
  - `mqtt_subscribe()`
  - `mqtt_connect()`
  - `mqtt_task()`

## 真实板业务层

- `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\real_board_business.h`
  - `real_board_business_append_telemetry()`
  - `real_board_business_append_status()`
  - `real_board_business_handle_json_command()`
  - `real_board_business_publish_event()`
  - `real_board_business_handle_legacy_command()`

- `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\real_board_business.c`
  - `append_event_payload()`
  - `real_board_business_append_telemetry()`
  - `real_board_business_append_status()`
  - `real_board_business_handle_json_command()`
  - `real_board_business_publish_event()`

## OTA

- `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\real_board_ota.h`
- `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\real_board_ota.c`

当前已有：

- `ota_begin`
- `ota_chunk`
- `ota_end`
- `ota_abort`
- `ota_status`
- 旧 B0-B6 兼容。

## 上位机

- `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\Management.cs`
  - Modbus 配置块序列化。
  - `BuildDefaultTopics()`。
  - topic/QoS 校验。

- `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\Form2.cs`
  - 单机设置窗口。

- `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\AdvancedSettingsForm.cs`
  - 批量设置窗口。

- `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\MqttClientForm.cs`
  - 内置 MQTT 客户端。
  - 多 topic 订阅/发布/报文解析。

## 测试

- `D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py`
- `D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_mqtt_lwt_status_contract.py`
- `D:\code\唐家湾嵌入式兼职\开发环境\host_tool_tests\test_net48_ui_contract.py`

## 已有文档

- `D:\code\唐家湾嵌入式兼职\docs\CUSTOMER_CURRENT_BUSINESS_AND_TOOL_USAGE_2026-07-02.md`
- `D:\code\唐家湾嵌入式兼职\docs\legacy_payload_single_topic_protocol_20260702.md`
- `D:\code\唐家湾嵌入式兼职\docs\MQTT_LWT_STATUS_TOPIC_2026-07-02.md`
