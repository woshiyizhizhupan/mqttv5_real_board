# 多主题 + 纯 JSON 完整源码评审包

日期：2026-07-03

这个包用于发给没有本机环境的 AI 或人工评审者。包内包含真实板固件源码、上位机源码、契约测试、必要脚本、协议方案文档和关键历史上下文文档。

## 推荐先读

1. `04_protocol_discussion/multi_topic_pure_json_discussion_20260703/README.md`
2. `04_protocol_discussion/multi_topic_pure_json_discussion_20260703/01_multi_topic_scheme.md`
3. `04_protocol_discussion/multi_topic_pure_json_discussion_20260703/02_pure_json_contract.md`
4. `04_protocol_discussion/multi_topic_pure_json_discussion_20260703/03_firmware_code_logic.md`
5. `04_protocol_discussion/multi_topic_pure_json_discussion_20260703/04_implementation_plan.md`

## 目录说明

```text
01_source/
  mqttv5_real_board/                 真实板 HC32F460 + W5500 固件源码和依赖

02_host_tool_source/
  mqttv5_tool/                       .NET Framework 4.8 WinForms 上位机源码

03_tests_and_scripts/
  firmware_tests/                    固件静态契约测试
  host_tool_tests/                   上位机静态契约测试
  host_tool_config_probe/            上位机配置探针源码
  host_tool_scan_probe/              上位机扫描探针源码
  host_mqtt_tester/                  MQTT 测试工具源码，未包含 .venv/build/dist
  flash_algo/                        安全烧录脚本依赖的 FLM 文件
  scripts/                           构建、安全烧录、证书和环境脚本

04_protocol_discussion/
  multi_topic_pure_json_discussion_20260703/
                                     多主题和纯 JSON 方案、示例、拟议 patch

05_reference_docs/                   当前业务合同和历史上下文文档
```

## 关键源码入口

固件：

- `01_source/mqttv5_real_board/src/config.h`
- `01_source/mqttv5_real_board/src/config.c`
- `01_source/mqttv5_real_board/src/main.c`
- `01_source/mqttv5_real_board/src/real_board_business.c`
- `01_source/mqttv5_real_board/src/real_board_hardware.c`
- `01_source/mqttv5_real_board/src/real_board_ota.c`
- `01_source/mqttv5_real_board/ldscripts/hc32f460_app_0x4000.lds`

上位机：

- `02_host_tool_source/mqttv5_tool/Management.cs`
- `02_host_tool_source/mqttv5_tool/Form1.cs`
- `02_host_tool_source/mqttv5_tool/Form2.cs`
- `02_host_tool_source/mqttv5_tool/AdvancedSettingsForm.cs`
- `02_host_tool_source/mqttv5_tool/MqttClientForm.cs`

测试：

- `03_tests_and_scripts/firmware_tests/test_real_board_business_contract.py`
- `03_tests_and_scripts/firmware_tests/test_mqtt_lwt_status_contract.py`
- `03_tests_and_scripts/host_tool_tests/test_net48_ui_contract.py`

## 当前建议方案摘要

推荐 5 个业务 topic 加 1 个独立状态 topic：

```text
v1/devices/telemetry/{device_id}
v1/devices/request/{device_id}
v1/devices/event/{device_id}
v1/devices/ota/{device_id}
v1/devices/debug/{device_id}
v1/devices/status/{device_id}
```

其中状态/LWT topic 不占 5 个业务 topic 配置槽，由固件按 `device_id` 自动生成。

## 已排除内容

为了便于直接发送给 AI/人工评审，本包排除了以下生成物：

- 固件 `.o/.obj/.elf/.bin/.hex/.map` 等构建产物。
- 上位机 `bin/obj`。
- MQTT 测试工具 `.venv/build/dist`。
- 历史生成烧录脚本 `开发环境/scripts/hc32_openocd_flash_mqttv5.generated.tcl`。

注意：排除构建产物不影响源码评审。`mqttv5_real_board/eide/build/Debug/builder.params`、`compile_commands.json` 等少量构建元数据已保留。

## 安全限制

不要覆盖 bootloader 区域：

```text
0x00000000 - 0x00003FFF
```

如果后续在本机真实烧录，只能使用安全脚本：

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Flash-MQTTv5-STLink.ps1' -Image '<app hex path>'
```

不要使用旧的 `hc32_openocd_flash_mqttv5.generated.tcl`，不要整片擦除 flash，不要更新 ST-LINKV2 固件。

## 本包状态

这是源码和方案评审包，不是已实施后的最终固件包。包内 `04_protocol_discussion/.../code/proposed_topic_routing_patch.diff` 是拟议改动片段，尚未应用到 `01_source/mqttv5_real_board`。
