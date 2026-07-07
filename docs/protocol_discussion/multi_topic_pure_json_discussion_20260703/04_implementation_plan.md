# Multi Topic Pure JSON Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the recommended 5-business-topic plus pure-JSON protocol for the real HC32F460 + W5500 firmware while preserving legacy payload compatibility.

**Architecture:** Keep `system_config_t` at five business topics and route publish/subscribe behavior by the existing `mqtt_topic_index_t` enum. Use pure JSON `schema/cmd/params` as the main service contract and keep fixed legacy payload frames behind the current compatibility path.

**Tech Stack:** HC32F460 C firmware, W5500 ioLibrary, Eclipse Paho MQTTPacket MQTT 5.0, cJSON, .NET Framework 4.8 WinForms host tool, Python unittest contract tests.

---

## File Structure

- Modify `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\config.c`
  - Add telemetry/event/ota/debug default topic prefixes.
  - Generate five distinct business topics in `config_reset()`.
- Modify `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\main.c`
  - Add topic helper functions.
  - Publish telemetry/event/response/debug to the correct topic index.
  - Subscribe command and OTA topics.
  - Add pure JSON command aliases if approved.
- Modify `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\real_board_business.c`
  - Accept `set_lighting` and `queue_event` aliases or route them from `main.c`.
- Modify `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\Management.cs`
  - Change default topic builder to telemetry/request/event/ota/debug.
- Modify tests under `D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests` and `D:\code\唐家湾嵌入式兼职\开发环境\host_tool_tests`
  - Update default topic contract and publish routing expectations.

## Task 1: Update Contract Tests First

- [ ] **Step 1: Update real-board topic contract test expectations**

Edit `D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py`:

```python
DOCUMENTED_TELEMETRY_TOPIC = "v1/devices/telemetry/{device_name}"
DOCUMENTED_REQUEST_TOPIC = "v1/devices/request/{device_name}"
DOCUMENTED_EVENT_TOPIC = "v1/devices/event/{device_name}"
DOCUMENTED_OTA_TOPIC = "v1/devices/ota/{device_name}"
DOCUMENTED_DEBUG_TOPIC = "v1/devices/debug/{device_name}"
```

In `test_mqtt_topics_match_request_response_server_contract`, replace assertions that require response/request duplication with assertions for:

```python
self.assertIn('REAL_BOARD_DEFAULT_TELEMETRY_TOPIC_PREFIX "v1/devices/telemetry/"', config_c)
self.assertIn('REAL_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX "v1/devices/request/"', config_c)
self.assertIn('REAL_BOARD_DEFAULT_EVENT_TOPIC_PREFIX "v1/devices/event/"', config_c)
self.assertIn('REAL_BOARD_DEFAULT_OTA_TOPIC_PREFIX "v1/devices/ota/"', config_c)
self.assertIn('REAL_BOARD_DEFAULT_DEBUG_TOPIC_PREFIX "v1/devices/debug/"', config_c)
self.assertIn("MQTT_TOPIC_TELEMETRY_UP_INDEX = 0", main_c)
self.assertIn("MQTT_TOPIC_CMD_DOWN_INDEX = 1", main_c)
self.assertIn("MQTT_TOPIC_EVENT_UP_INDEX = 2", main_c)
self.assertIn("MQTT_TOPIC_OTA_INDEX = 3", main_c)
self.assertIn("MQTT_TOPIC_DEBUG_UP_INDEX = 4", main_c)
self.assertRegex(main_c, r"mqtt_publish_index_json\s*\(\s*MQTT_TOPIC_TELEMETRY_UP_INDEX")
self.assertRegex(main_c, r"mqtt_publish_index_json\s*\(\s*MQTT_TOPIC_EVENT_UP_INDEX")
self.assertRegex(main_c, r"mqtt_publish_index_json\s*\(\s*MQTT_TOPIC_DEBUG_UP_INDEX")
self.assertRegex(main_c, r"mqtt_subscribe_configured_topic\s*\(\s*MQTT_TOPIC_CMD_DOWN_INDEX")
self.assertRegex(main_c, r"mqtt_subscribe_configured_topic\s*\(\s*MQTT_TOPIC_OTA_INDEX")
```

- [ ] **Step 2: Run the updated test and confirm it fails**

Run:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester\.venv\Scripts\python.exe' -m unittest 'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py'
```

Expected: FAIL because firmware still uses request/response duplicated defaults and response-topic publish helper.

## Task 2: Change Real Firmware Default Topics

- [ ] **Step 1: Add default topic prefixes**

Modify `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\config.c` near existing default prefix macros:

```c
#ifndef REAL_BOARD_DEFAULT_TELEMETRY_TOPIC_PREFIX
#define REAL_BOARD_DEFAULT_TELEMETRY_TOPIC_PREFIX "v1/devices/telemetry/"
#endif

#ifndef REAL_BOARD_DEFAULT_EVENT_TOPIC_PREFIX
#define REAL_BOARD_DEFAULT_EVENT_TOPIC_PREFIX "v1/devices/event/"
#endif

#ifndef REAL_BOARD_DEFAULT_OTA_TOPIC_PREFIX
#define REAL_BOARD_DEFAULT_OTA_TOPIC_PREFIX "v1/devices/ota/"
#endif

#ifndef REAL_BOARD_DEFAULT_DEBUG_TOPIC_PREFIX
#define REAL_BOARD_DEFAULT_DEBUG_TOPIC_PREFIX "v1/devices/debug/"
#endif
```

Keep:

```c
#ifndef REAL_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX
#define REAL_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX "v1/devices/request/"
#endif
```

- [ ] **Step 2: Update `system_config_def.mqtt_server.topics` placeholders**

Use:

```c
.mqtt_server.topics = {
    "v1/devices/telemetry/{device_name}",
    "v1/devices/request/{device_name}",
    "v1/devices/event/{device_name}",
    "v1/devices/ota/{device_name}",
    "v1/devices/debug/{device_name}",
},
```

- [ ] **Step 3: Update `config_reset()` topic generation**

Replace `request_topic` / `response_topic` only variables with five concrete buffers:

```c
char telemetry_topic[MQTT_TOPIC_LEN];
char request_topic[MQTT_TOPIC_LEN];
char event_topic[MQTT_TOPIC_LEN];
char ota_topic[MQTT_TOPIC_LEN];
char debug_topic[MQTT_TOPIC_LEN];
```

Generate:

```c
snprintf(telemetry_topic, sizeof(telemetry_topic), "%s%s", REAL_BOARD_DEFAULT_TELEMETRY_TOPIC_PREFIX, system_config_temp.device_id);
snprintf(request_topic, sizeof(request_topic), "%s%s", REAL_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX, system_config_temp.device_id);
snprintf(event_topic, sizeof(event_topic), "%s%s", REAL_BOARD_DEFAULT_EVENT_TOPIC_PREFIX, system_config_temp.device_id);
snprintf(ota_topic, sizeof(ota_topic), "%s%s", REAL_BOARD_DEFAULT_OTA_TOPIC_PREFIX, system_config_temp.device_id);
snprintf(debug_topic, sizeof(debug_topic), "%s%s", REAL_BOARD_DEFAULT_DEBUG_TOPIC_PREFIX, system_config_temp.device_id);
```

Copy into topic slots 0-4.

- [ ] **Step 4: Run the topic contract test**

Run:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester\.venv\Scripts\python.exe' -m unittest 'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py'
```

Expected: topic default assertions pass; publish routing assertions still fail until Task 3.

## Task 3: Route MQTT Publish/Subscribe by Topic Index

- [ ] **Step 1: Add helper functions in `main.c`**

Add after `mqtt_get_response_topic()`:

```c
static const char *mqtt_get_configured_topic(mqtt_topic_index_t topic_index)
{
    if (((uint8_t)topic_index < MQTT_TOPIC_COUNT) &&
        (system_config->mqtt_server.topics[topic_index][0] != '\0'))
        return system_config->mqtt_server.topics[topic_index];

    return mqtt_get_response_topic();
}

static int mqtt_publish_index_json(mqtt_topic_index_t topic_index, const char *json)
{
    return mqtt_publish_json_to_topic(mqtt_get_configured_topic(topic_index),
                                      json,
                                      system_config->mqtt_server.qos[topic_index]);
}

static uint8_t mqtt_subscribe_configured_topic(mqtt_topic_index_t topic_index)
{
    return mqtt_subscribe((char *)mqtt_get_configured_topic(topic_index),
                          system_config->mqtt_server.qos[topic_index]);
}
```

- [ ] **Step 2: Change publishing call sites**

Use:

```c
mqtt_publish_index_json(MQTT_TOPIC_EVENT_UP_INDEX, mqtt_json_buf);
mqtt_publish_index_json(MQTT_TOPIC_DEBUG_UP_INDEX, mqtt_json_buf);
mqtt_publish_index_json(MQTT_TOPIC_TELEMETRY_UP_INDEX, mqtt_json_buf);
```

Specifically:

- `mqtt_publish_response()` -> event topic.
- `mqtt_publish_legacy_payload_envelope_response()` -> event topic.
- `mqtt_publish_status()` -> event topic unless service wants status topic for transient status.
- `mqtt_publish_debug()` -> debug topic.
- `mqtt_publish_telemetry()` -> telemetry topic.
- `mqtt_publish_real_event()` -> event topic.

- [ ] **Step 3: Subscribe command and OTA topics**

In `mqtt_connect()`, replace the single subscribe with:

```c
mqtt_subscribe_configured_topic(MQTT_TOPIC_CMD_DOWN_INDEX);
if (strcmp(mqtt_get_configured_topic(MQTT_TOPIC_OTA_INDEX),
           mqtt_get_configured_topic(MQTT_TOPIC_CMD_DOWN_INDEX)) != 0)
{
    mqtt_subscribe_configured_topic(MQTT_TOPIC_OTA_INDEX);
}
```

- [ ] **Step 4: Run contract tests**

Run:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester\.venv\Scripts\python.exe' -m unittest 'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py' 'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_mqtt_lwt_status_contract.py'
```

Expected: OK after test regexes match the new helpers.

## Task 4: Add Pure JSON Business Aliases

- [ ] **Step 1: Add command alias handling**

In `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\main.c`, extend the current real board command branch:

```c
else if ((strcmp(cmd, "set_lighting") == 0) ||
         (strcmp(cmd, "queue_event") == 0) ||
         (strcmp(cmd, "real_set") == 0) ||
         (strcmp(cmd, "real_event") == 0) ||
         (strcmp(cmd, "sim_set") == 0) ||
         (strcmp(cmd, "sim_event") == 0))
```

Pass an internal command name to `real_board_business_handle_json_command()`:

```c
const char *business_cmd = cmd;
if (strcmp(cmd, "set_lighting") == 0)
    business_cmd = "real_set";
else if (strcmp(cmd, "queue_event") == 0)
    business_cmd = "real_event";
```

- [ ] **Step 2: Add test coverage**

In `test_real_board_business_contract.py`, assert:

```python
self.assertIn('"set_lighting"', main_c)
self.assertIn('"queue_event"', main_c)
self.assertIn('business_cmd = "real_set"', main_c)
self.assertIn('business_cmd = "real_event"', main_c)
```

- [ ] **Step 3: Run tests**

Run:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester\.venv\Scripts\python.exe' -m unittest 'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py'
```

Expected: OK.

## Task 5: Update Host Tool Defaults

- [ ] **Step 1: Update `Management.BuildDefaultTopics()`**

In `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\Management.cs`, change default topics to:

```csharp
topics[0] = "v1/devices/telemetry/" + deviceName;
topics[1] = "v1/devices/request/" + deviceName;
topics[2] = "v1/devices/event/" + deviceName;
topics[3] = "v1/devices/ota/" + deviceName;
topics[4] = "v1/devices/debug/" + deviceName;
```

- [ ] **Step 2: Update host tool contract test**

In `D:\code\唐家湾嵌入式兼职\开发环境\host_tool_tests\test_net48_ui_contract.py`, update default topic assertions to the five-topic mapping.

- [ ] **Step 3: Run host tests**

Run:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester\.venv\Scripts\python.exe' -m unittest 'D:\code\唐家湾嵌入式兼职\开发环境\host_tool_tests\test_net48_ui_contract.py'
```

Expected: OK.

## Task 6: Build and Verify

- [ ] **Step 1: Build real firmware**

Run:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Build-RealBoard-Firmware.ps1' -Clean
```

Expected: build succeeds and HEX range starts at `0x00004000`.

- [ ] **Step 2: Build host tool**

Run:

```powershell
dotnet build 'D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\mqttv5_tool.csproj' -v:minimal
```

Expected: `0 warnings / 0 errors`.

- [ ] **Step 3: Run combined contract tests**

Run:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester\.venv\Scripts\python.exe' -m unittest `
  'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py' `
  'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_mqtt_lwt_status_contract.py' `
  'D:\code\唐家湾嵌入式兼职\开发环境\host_tool_tests\test_net48_ui_contract.py'
```

Expected: OK.

## Task 7: Field Validation

- [ ] **Step 1: Configure a board with multi-topic defaults**

Use the host tool or Modbus probe to write:

```text
v1/devices/telemetry/GM400-452089
v1/devices/request/GM400-452089
v1/devices/event/GM400-452089
v1/devices/ota/GM400-452089
v1/devices/debug/GM400-452089
```

- [ ] **Step 2: Subscribe from service or MQTT client**

Subscribe:

```text
v1/devices/telemetry/+
v1/devices/event/+
v1/devices/debug/+
v1/devices/status/+
```

- [ ] **Step 3: Publish pure JSON command**

Publish to:

```text
v1/devices/request/GM400-452089
```

Payload:

```json
{"schema":"emqx-gateway.command.v1","id":"smoke-1","device_id":"GM400-452089","cmd":"ping","params":{}}
```

Expected: response appears on `v1/devices/event/GM400-452089`.

- [ ] **Step 4: Confirm telemetry route**

Expected: periodic telemetry appears on `v1/devices/telemetry/GM400-452089` only.

- [ ] **Step 5: Confirm status route**

Expected: retained online/offline status appears on `v1/devices/status/GM400-452089`.

## Task 8: Safe Flash

Only after build and tests pass, flash with the project safe script. Never use old generated TCL scripts or whole-chip erase.

Run:

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Flash-MQTTv5-STLink.ps1' -Image 'D:\code\唐家湾嵌入式兼职\mqttv5_real_board\eide\build\Debug\mqttv5_real_board.hex'
```

Expected:

```text
APP verify passed at 0x00004000
bootloader region 0x00000000-0x00003FFF unchanged
```
