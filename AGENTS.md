# AGENTS.md

本文件约束后续 Codex/AI 代理在本仓库内工作。除非用户明确覆盖，优先遵守这些说明。

## 环境

- 这是 Windows 本地项目，优先使用 PowerShell 7：`pwsh -NoLogo -NoProfile -Command "<command>"`。
- PowerShell 7 路径通常是 `C:\Users\96932\AppData\Local\Microsoft\WindowsApps\pwsh.exe`。
- 不要修改 `ComSpec`，保持 Windows 内部使用 `cmd.exe`。
- 搜索文件和文本优先用 `rg` / `rg --files`。

## 项目边界

- 根目录是源码评审包整理版，不要随意移动目录。许多脚本和静态测试依赖当前路径结构。
- 当前主要业务源码在：
  - `mqttv5_real_board/src/`
  - `mqttv5_real_board/driver/`
  - `mqttv5_sim_board/src/`
  - `mqttv5_tool/mqttv5_tool/`
  - `emqx_device_emulator_tool/`
  - `real_board_mqtt_emulator/`
  - `开发环境/host_mqtt_tester/`
- 第三方/底层库目录如 `mbedtls/`、`ioLibrary_Driver-3.2.0/`、`libraries/`、`mqtt/`、`modbus/`、`cjson/` 只在必要时修改。

## 真实板安全

- 绝不整片擦除 MCU flash。
- 绝不写入 bootloader 区域 `0x00000000-0x00003FFF`。
- 不要使用旧生成脚本 `开发环境/scripts/hc32_openocd_flash_mqttv5.generated.tcl`。
- 不要更新 ST-LINK 固件。
- 真实板烧录只使用 `开发环境/scripts/Flash-MQTTv5-STLink.ps1 -Image '<app hex path>'`，它会校验 bootloader 区未变化。
- APP1 链接起始地址是 `0x00004000`，对应 `mqttv5_real_board/ldscripts/hc32f460_app_0x4000.lds`。

## 敏感信息

- 不要提交证书、私钥、真实密码或现场专用凭据。
- `mqttv5_real_board/src/real_board_tls_certs.c` 被忽略，可能包含 TLS 客户端私钥字面量。
- `.gitignore` 已忽略 `*.pem`、`*.key`、`*.p12`、`*.pfx`、`aliyun_mqtt_private/` 和 `**/secrets/`。

## 当前协议状态

- `docs/protocol_discussion/multi_topic_pure_json_discussion_20260703/` 是方案讨论包。
- `code/proposed_topic_routing_patch.diff` 是拟议补丁，不是已应用补丁。
- 当前真实板主源码已支持 JSON 命令、状态/LWT topic、Legacy 帧兼容和 OTA 基础能力。
- 当前 `config_reset()` 默认仍主要写 request/response topic：topic 0/2/4 为 response，topic 1/3 为 request。
- 完整 telemetry/request/event/ota/debug 五主题默认值、按槽位发布、同时订阅 cmd/ota，是后续实现目标；改这些行为时必须同步测试和文档。

## 常用命令

真实板构建：

```powershell
pwsh -NoLogo -NoProfile -File .\开发环境\scripts\Build-RealBoard-Firmware.ps1 -Clean
```

模拟板构建：

```powershell
pwsh -NoLogo -NoProfile -File .\开发环境\scripts\Build-SimBoard-Firmware.ps1 -Clean
```

上位机构建：

```powershell
dotnet build .\mqttv5_tool\mqttv5_tool\mqttv5_tool.csproj -v:minimal
dotnet build .\emqx_device_emulator_tool\EmqxDeviceEmulator.App\EmqxDeviceEmulator.App.csproj -v:minimal
```

契约测试：

```powershell
python -m unittest discover -s .\开发环境\firmware_tests
python -m unittest discover -s .\开发环境\host_tool_tests
python -m unittest discover -s .\real_board_mqtt_emulator\tests
python -m unittest discover -s .\开发环境\host_mqtt_tester\tests
dotnet run --project .\emqx_device_emulator_tool\EmqxDeviceEmulator.Tests\EmqxDeviceEmulator.Tests.csproj
```

## 修改规则

- 修改固件协议、topic、JSON schema、OTA 或配置结构时，同步更新 README、协议文档和契约测试。
- 修改 `system_config_t` 布局、默认值或校验逻辑时，明确评估 `SYSTEM_CONFIG_VERSION` 是否需要提升。
- 修改上位机配置读写时，检查 `Management.cs` 的长度、QoS、静态网络、MAC、TLS/NTP 字段校验。
- 修改构建脚本时，保留安全检查，尤其是 HEX 起始地址和 bootloader 保护。
- 不要删除历史参考文档，除非用户明确要求。
