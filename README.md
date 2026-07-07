# EMQX Gateway Multi-Topic Pure JSON Project

本仓库是 `multi_topic_pure_json_full_source_20260703` 源码评审包整理后的项目副本，面向 HC32F460 + W5500 真实板、模拟板、EMQX MQTT 5.0 联调工具、上位机工具和契约测试。

当前仓库的重点不是单一固件工程，而是一套完整交付材料：固件源码、Windows 上位机、设备端模拟器、Python MQTT 测试工具、协议讨论文档、构建/烧录/验证脚本和历史上下文文档。

## 当前状态

- 真实板固件位于 `mqttv5_real_board/`，目标 MCU 为 HC32F460，APP1 链接起始地址为 `0x00004000`，保留 `0x00000000-0x00003FFF` bootloader 区域。
- 模拟板固件位于 `mqttv5_sim_board/`，用于静态契约和兼容路径验证。
- 根目录 `README_FULL_SOURCE_PACKAGE.md` 保留原评审包说明；本文件是当前仓库的总说明。
- `docs/protocol_discussion/multi_topic_pure_json_discussion_20260703/` 是“多主题 + 纯 JSON”方案讨论包，其中 `code/proposed_topic_routing_patch.diff` 是拟议补丁，不是已经应用到主源码的补丁。
- 当前主源码已经具备 JSON 命令、真实板 telemetry/event/status、LWT 状态 topic、Legacy A1/A2/A5/B0-B6 兼容、mTLS/OTA 相关基础能力。
- 当前主源码默认 topic 仍主要是 request/response 形态：`config_reset()` 将 topic 0/2/4 设为 `v1/devices/response/{device_id}`，topic 1/3 设为 `v1/devices/request/{device_id}`。完整的 telemetry/request/event/ota/debug 五主题默认值仍属于讨论包中的后续改造目标。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| `mqttv5_real_board/` | 真实板 HC32F460 + W5500 固件源码、EIDE/MDK 元数据、链接脚本、HDSC pack、WIZnet ioLibrary、mbedTLS、cJSON、MQTT packet、硬件驱动。 |
| `mqttv5_sim_board/` | 模拟板固件源码和依赖，用于保留合同测试覆盖。 |
| `mqttv5_tool/` | .NET Framework 4.8 WinForms 上位机，支持设备扫描、网络/MQTT 配置、五 topic、QoS、TLS/NTP 字段。 |
| `emqx_device_emulator_tool/` | C# WinForms 设备端模拟器和协议测试工程，用来模拟 EMQX 设备端响应。 |
| `real_board_mqtt_emulator/` | Python 真实板 MQTT 模拟器，发布真实板 schema 的 telemetry/status/event/debug/response。 |
| `开发环境/` | 固件/上位机契约测试、host MQTT tester、探针工程、flash 依赖、构建/烧录/证书/OTA 脚本。 |
| `docs/` | 协议讨论、交接记录、真实板硬件接入记录、历史业务合同和参考文档。 |
| `PACKAGE_MANIFEST.original_package.txt` | 原包文件清单。 |

第三方或底层库主要在 `mbedtls/`、`ioLibrary_Driver-3.2.0/`、`libraries/`、`mqtt/`、`modbus/`、`cjson/` 下。修改业务逻辑时优先动 `src/`、`driver/`、上位机源码和测试，不要无谓重排这些库目录。

## 核心模块

### 真实板固件

入口和关键文件：

- `mqttv5_real_board/src/main.c`：MQTT 5.0 连接、TLS、LWT、订阅、发布、JSON 命令分发、Modbus 管理通道。
- `mqttv5_real_board/src/config.h` / `config.c`：网络、MQTT、五 topic、QoS、TLS/NTP 配置结构和 flash 保存逻辑。真实板当前 `SYSTEM_CONFIG_VERSION` 默认是 `3`。
- `mqttv5_real_board/src/real_board_business.c`：真实板 telemetry/event/status JSON、灯控状态、RS485/HLW8112/环境数据汇总、Legacy A1/A2/A5 兼容。
- `mqttv5_real_board/src/real_board_hardware.c`：USART/RS485/HLW8112 等真实硬件采集和透传。
- `mqttv5_real_board/src/real_board_ota.c`：JSON OTA 和 Legacy B0-B6 OTA。
- `mqttv5_real_board/src/real_board_tls.c`：mTLS 证书入口；私有证书生成文件 `src/real_board_tls_certs.c` 被 `.gitignore` 忽略。
- `mqttv5_real_board/ldscripts/hc32f460_app_0x4000.lds`：APP1 从 `0x00004000` 链接，避免覆盖 bootloader。

重要 flash 地址：

| 区域 | 地址 |
| --- | --- |
| Bootloader 保留区 | `0x00000000-0x00003FFF` |
| APP1 起始 | `0x00004000` |
| APP2 OTA 起始 | `0x00042000` |
| APP2 OTA 结束 | `0x00076000` |
| OTA 升级状态 | `0x0007A000` |
| 配置区 | flash 末尾前第 5 个 8KB sector |

### 上位机工具

`mqttv5_tool/mqttv5_tool/` 是 .NET Framework 4.8 WinForms 项目：

- `Management.cs`：Modbus/TCP 配置读写、扫描、校验、保存重启。
- `Form1.cs`：主界面、扫描、设备列表、批量配置。
- `Form2.cs`、`AdvancedSettingsForm.cs`：单设备和高级 MQTT 配置界面。
- `MqttClientForm.cs`：内置 MQTT 客户端。

依赖：

- `MQTTnet` `4.3.7.1207`
- `NModbus` `3.0.81`
- Windows/.NET Framework 4.8

### 模拟器和测试工具

- `emqx_device_emulator_tool/`：C# 设备端 GUI 模拟器，默认连接远端 EMQX，兼容 EMQX payload 外壳和 Legacy OTA 帧。
- `real_board_mqtt_emulator/`：Python 真实板 MQTT 模拟器，默认合同见该目录 README。
- `开发环境/host_mqtt_tester/`：Python MQTT CLI/GUI 测试工具和打包入口。
- `开发环境/host_tool_config_probe/`、`host_tool_scan_probe/`：给脚本使用的 .NET 配置/扫描探针。

## MQTT 与协议

推荐目标方案见：

- `docs/protocol_discussion/multi_topic_pure_json_discussion_20260703/01_multi_topic_scheme.md`
- `docs/protocol_discussion/multi_topic_pure_json_discussion_20260703/02_pure_json_contract.md`
- `docs/protocol_discussion/multi_topic_pure_json_discussion_20260703/03_firmware_code_logic.md`

目标方案是 5 个业务 topic + 1 个独立状态 topic：

| 类型 | 方向 | 目标 topic |
| --- | --- | --- |
| 遥测 | 设备 -> 服务端 | `v1/devices/telemetry/{device_id}` |
| 命令 | 服务端 -> 设备 | `v1/devices/request/{device_id}` |
| 响应/事件 | 设备 -> 服务端 | `v1/devices/event/{device_id}` |
| OTA 下行 | 服务端 -> 设备 | `v1/devices/ota/{device_id}` |
| 调试/兼容 | 设备 -> 服务端 | `v1/devices/debug/{device_id}` |
| 状态/LWT | 设备 -> 服务端 | `v1/devices/status/{device_id}` |

当前主源码中，状态/LWT topic 已经独立生成，retain=true、QoS 0，不占五个配置 topic 槽。普通命令当前订阅 request topic；完整订阅 OTA topic 和按五槽分流发布属于后续实现计划。

常见 JSON 命令：

- `ping`
- `get_status`
- `get_config`
- `set_config`
- `reboot`
- `real_set`
- `real_event`
- `ota_begin`
- `ota_chunk`
- `ota_end`
- `ota_abort`
- `ota_status`
- `legacy_frame`

Legacy 兼容仍保留，包括 `payload` 十六进制/Base64 旧帧、A1/A2/A5 业务帧、B0-B6 OTA 帧。可以通过构建定义 `REAL_BOARD_ENABLE_LEGACY_FRAME=0` 关闭旧帧解析。

## 本地构建

建议使用 PowerShell 7：

```powershell
pwsh -NoLogo -NoProfile
```

真实板 GNU Arm GCC 构建：

```powershell
pwsh -NoLogo -NoProfile -File .\开发环境\scripts\Build-RealBoard-Firmware.ps1 -Clean
```

模拟板 GNU Arm GCC 构建：

```powershell
pwsh -NoLogo -NoProfile -File .\开发环境\scripts\Build-SimBoard-Firmware.ps1 -Clean
```

默认 GCC 路径是：

```text
D:\Tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-gcc.exe
```

如工具链不在默认路径，传入 `-Gcc`：

```powershell
pwsh -NoLogo -NoProfile -File .\开发环境\scripts\Build-RealBoard-Firmware.ps1 `
  -Gcc 'D:\Tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-gcc.exe' `
  -Clean
```

输出通常在：

```text
mqttv5_real_board/eide/build/LocalEMQX/
mqttv5_sim_board/eide/build/LocalEMQX/
```

上位机构建：

```powershell
dotnet build .\mqttv5_tool\mqttv5_tool\mqttv5_tool.csproj -v:minimal
dotnet build .\emqx_device_emulator_tool\EmqxDeviceEmulator.App\EmqxDeviceEmulator.App.csproj -v:minimal
dotnet run --project .\emqx_device_emulator_tool\EmqxDeviceEmulator.Tests\EmqxDeviceEmulator.Tests.csproj
```

## 测试

固件和上位机静态契约测试：

```powershell
python -m unittest discover -s .\开发环境\firmware_tests
python -m unittest discover -s .\开发环境\host_tool_tests
```

Python MQTT 工具测试：

```powershell
python -m unittest discover -s .\real_board_mqtt_emulator\tests
python -m unittest discover -s .\开发环境\host_mqtt_tester\tests
```

`开发环境/host_mqtt_tester` 和 `real_board_mqtt_emulator` 的运行依赖都是：

```text
paho-mqtt==2.1.0
```

可以按目录创建本地虚拟环境：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

## 烧录与 OTA

只能使用安全烧录脚本写 APP 区：

```powershell
pwsh -NoLogo -NoProfile -File .\开发环境\scripts\Flash-MQTTv5-STLink.ps1 -Image '<app hex path>'
```

安全边界：

- 不要整片擦除 flash。
- 不要写入 `0x00000000-0x00003FFF`。
- 不要使用旧的 `开发环境/scripts/hc32_openocd_flash_mqttv5.generated.tcl`。
- 不要更新 ST-LINK 固件。
- 烧录脚本会读取烧录前后的 bootloader 区并比较，确认 bootloader 未变化。

mTLS provisioning 和 OTA 验证脚本：

- `开发环境/scripts/Provision-RealBoardMTLSFirmware.ps1`
- `开发环境/scripts/Run-RealBoardMtlsOtaValidation.ps1`
- `开发环境/scripts/Read-RealBoardApp2.ps1`

这些脚本依赖真实板、EMQX、证书目录、OpenOCD/ST-LINK 和本地工具链；运行前先读脚本参数，不要直接套用旧绝对路径。

## 敏感文件

`.gitignore` 已忽略：

- `mqttv5_real_board/src/real_board_tls_certs.c`
- `*.pem`
- `*.key`
- `*.p12`
- `*.pfx`
- `aliyun_mqtt_private/`
- `**/secrets/`

`real_board_tls_certs.c` 可能包含 TLS 客户端私钥字面量。公开发布或交给第三方前，必须改为模板或通过私有渠道注入证书。

## 常用阅读顺序

第一次接手建议按以下顺序读：

1. 本 README。
2. `README_FULL_SOURCE_PACKAGE.md`。
3. `docs/protocol_discussion/multi_topic_pure_json_discussion_20260703/README.md`。
4. `docs/protocol_discussion/multi_topic_pure_json_discussion_20260703/02_pure_json_contract.md`。
5. `docs/protocol_discussion/multi_topic_pure_json_discussion_20260703/03_firmware_code_logic.md`。
6. `mqttv5_real_board/docs/REAL_BOARD_HARDWARE_SYNC_20260702.md`。
7. `开发环境/firmware_tests/` 和 `开发环境/host_tool_tests/` 中的契约测试。

## 维护建议

- 修改协议或 topic 行为时，同步更新协议文档、固件测试和上位机测试。
- 修改 `system_config_t` 布局或默认配置时，评估是否提升 `SYSTEM_CONFIG_VERSION`，否则旧 flash 配置会继续生效。
- 修改真实板构建定义时，优先通过 `Build-RealBoard-Firmware.ps1 -AdditionalDefine` 注入，不要把现场私有参数硬编码进源码。
- 修改上位机配置读写时，保持 `Management.cs` 的长度、QoS、静态网络和 MAC 校验。
- 只在确有必要时修改第三方库；业务变化优先落在 `src/`、`driver/`、工具层和测试层。
