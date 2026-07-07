# MQTT LWT status topic integration - 2026-07-02

## Requirement

Status topic uses the concrete device ID:

- Device publish topic: `v1/devices/status/{device_id}`
- Server subscribe topic: `v1/devices/status/+`
- The device must not publish to `+`. `+` is only a subscribe wildcard.

MQTT CONNECT registers an LWT message:

```json
{"schema":"emqx-gateway.status.v1","device_id":"GM400-452089","status":"offline","reason":"mqtt_lwt"}
```

After CONNACK succeeds, firmware publishes online status:

```json
{"schema":"emqx-gateway.status.v1","device_id":"GM400-452089","status":"online","reason":"mqtt_connected"}
```

Both LWT offline and online status use QoS 0 and retain=true.

## Implementation

Changed files:

- `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\main.c`
- `D:\code\唐家湾嵌入式兼职\mqttv5_sim_board\src\main.c`
- `D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py`
- `D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_mqtt_lwt_status_contract.py`

Key behavior:

- Added `MQTT_SERVER_STATUS_TOPIC_PREFIX "v1/devices/status/"`.
- Added `mqtt_get_status_topic()` to generate `v1/devices/status/{device_id}` from the current configured device ID.
- Added `mqtt_make_status_payload(status, reason)` for the server status JSON.
- Added `mqtt_publish_json_to_topic_ex(..., retain_flag)` so lifecycle status can publish retained messages without changing normal business publish behavior.
- Added `mqtt_publish_lifecycle_status("online", "mqtt_connected")` after MQTT connection succeeds.
- Added MQTT CONNECT LWT:
  - `data.willFlag = 1`
  - `data.will.qos = 0`
  - `data.will.retained = 1`
  - `data.will.topicName.cstring = (char *)mqtt_get_status_topic()`
  - `data.will.message.cstring = mqtt_make_status_payload("offline", "mqtt_lwt")`

Compatibility:

- Existing 5 business topics are unchanged.
- `MQTT_TOPIC_COUNT` remains `5`.
- Modbus configuration block and upper-computer 5-topic UI contract are unchanged.
- `v1/devices/status/+` is not stored as a firmware business topic.

## Verification

Build commands:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Build-RealBoard-Firmware.ps1' -Clean
& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Build-SimBoard-Firmware.ps1' -Clean
```

Build result:

- Real board firmware built successfully.
  - HEX range: `0x00004000-0x0002F7C3`
- Sim board firmware built successfully.
  - HEX range: `0x00004000-0x0001AE77`

Contract tests:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester\.venv\Scripts\python.exe' -m unittest `
  'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_local_emqx_firmware_config.py' `
  'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py' `
  'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_sim_board_ota_contract.py' `
  'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_mqtt_lwt_status_contract.py' `
  'D:\code\唐家湾嵌入式兼职\开发环境\host_tool_tests\test_net48_ui_contract.py'
```

Test result:

- `Ran 85 tests`
- `OK`

Flash command:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Flash-MQTTv5-STLink.ps1' -Image 'D:\code\唐家湾嵌入式兼职\mqttv5_sim_board\eide\build\LocalEMQX\mqttv5_sim_board.hex'
```

Flash result:

- APP range written: `0x00004000 - 0x0001C000`
- Host-side APP verify passed at `0x00004000`.
- Bootloader region `0x00000000-0x00003FFF` unchanged.
- Follow-up `reset run` completed successfully.

Remote broker observation:

- A monitor client connected to `39.103.154.108:1883` using `GM400-452089/public`.
- It subscribed to `v1/devices/status/+`.
- No retained status message was received in the 15-second observation window.
- This does not invalidate the firmware build or flash result. It means the currently flashed board did not publish retained status to that remote broker during the observation window. Check the board's persisted MQTT host/port/username/password configuration if live remote observation is required.

## Server-side usage

Server subscribes:

```text
v1/devices/status/+
```

Expected online message example:

```text
topic: v1/devices/status/GM400-452089
payload: {"schema":"emqx-gateway.status.v1","device_id":"GM400-452089","status":"online","reason":"mqtt_connected"}
retain: true
qos: 0
```

Expected abnormal offline message example:

```text
topic: v1/devices/status/GM400-452089
payload: {"schema":"emqx-gateway.status.v1","device_id":"GM400-452089","status":"offline","reason":"mqtt_lwt"}
retain: true
qos: 0
```
