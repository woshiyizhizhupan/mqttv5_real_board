# 项目交接说明：HC32F460 + W5500 网关接入 EMQX

更新时间：2026-06-28  
工作目录：`D:\code\唐家湾嵌入式兼职`

## 一、用户目标

当前目标是把原有 HC32F460 + W5500 网关项目从阿里云 IoT 方案切换为直连本地/局域网 EMQX，并提供 .NET Framework 4.8 上位机用于局域网管理。

明确要求：

- 不再接入阿里云，删除/停用阿里云三元组、`securemode` 等配置逻辑。
- MQTT 协议使用 MQTT 5.0，直连 EMQX。
- 业务主题拆成 5 个主题。
- W5500 使用两个 socket：
  - socket0：MQTT/EMQX 业务连接。
  - socket1：保留现有 Modbus TCP 502 管理通道，用于查找、重启、单机配置、批量配置。
- 上位机按 `网关上位机界面.docx` 的界面要求实现，目标框架为 `.NET Framework 4.8`。
- 使用 ST-LINKV2 烧录，但不要更新 ST-LINK 固件版本。
- 固件从 `0x4000` 开始，不能覆盖 bootloader 区域。

## 二、当前实现状态

已完成：

- 固件默认服务器改为 EMQX：`192.168.0.110:1883`。
- 固件配置结构已扩展为：
  - `device_id[32]`
  - `mqtt_server.host[64]`
  - `mqtt_server.port`
  - `mqtt_server.username[32]`
  - `mqtt_server.password[32]`
  - `mqtt_server.topics[5][96]`
  - `mqtt_server.qos[5]`
  - `mqtt_server.ntp_server[64]`
- 五个默认主题：
  - `/local_pk/hc32f460_dev/user/telemetry/up`
  - `/local_pk/hc32f460_dev/user/cmd/down`
  - `/local_pk/hc32f460_dev/user/event/up`
  - `/local_pk/hc32f460_dev/user/status/up`
  - `/local_pk/hc32f460_dev/user/debug/up`
- MQTT 连接使用 MQTT 5 packet 库，client id 默认由设备 ID 生成，如 `GM400-452089`。
- W5500 MQTT socket 根据 flash 配置中的 host/port 动态连接，支持 IPv4 字符串和 DNS。
- Modbus TCP 仍暴露完整配置块，上位机通过 502 端口管理。
- 上位机已迁移到 `.NET Framework 4.8 WinForms`。
- 上位机已实现：
  - 查找
  - 重启设备
  - 单机设置
  - 高级设置/批量设置
  - INI 保存/加载
  - 中文界面文本和基础英文语言选项

## 三、关键文件

固件：

- `D:\code\唐家湾嵌入式兼职\mqttv5(1)\src\config.h`
  - 配置结构、字段长度、主题数量。
- `D:\code\唐家湾嵌入式兼职\mqttv5(1)\src\config.c`
  - 默认 EMQX 地址、默认主题、默认设备 ID。
- `D:\code\唐家湾嵌入式兼职\mqttv5(1)\src\task_w5500.c`
  - W5500 socket0/socket1 配置，MQTT 目标地址应用逻辑。
- `D:\code\唐家湾嵌入式兼职\mqttv5(1)\src\main.c`
  - MQTT 5.0 连接、订阅五个主题、telemetry 上报、Modbus 任务。
- `D:\code\唐家湾嵌入式兼职\mqttv5(1)\ldscripts\hc32f460_app_0x4000.lds`
  - 应用区链接脚本，FLASH 从 `0x00004000` 开始。

上位机：

- `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\mqttv5_tool.csproj`
  - `.NET Framework 4.8` 项目文件。
- `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\Management.cs`
  - Modbus TCP 发现、读写配置、重启、序列化配置块。
- `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\Form1.cs`
  - 主界面逻辑。
- `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\Form1.Designer.cs`
  - 主界面布局。
- `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\Form2.cs`
  - 单设备设置窗口。
- `D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\AdvancedSettingsForm.cs`
  - 高级/批量设置窗口。

工具与测试：

- `D:\code\唐家湾嵌入式兼职\开发环境\scripts\Build-LocalEMQX-Firmware.ps1`
  - 构建本地 EMQX 固件。
- `D:\code\唐家湾嵌入式兼职\开发环境\scripts\build_local_emqx_firmware.py`
  - GNU Arm GCC 构建逻辑，已修复 Intel HEX type 02 扩展段地址解析。
- `D:\code\唐家湾嵌入式兼职\开发环境\scripts\Flash-MQTTv5-STLink.ps1`
  - 安全烧录脚本，只写 `0x4000` 之后应用区，并回读校验 APP 与 bootloader。
- `D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_local_emqx_firmware_config.py`
  - 固件静态契约测试。
- `D:\code\唐家湾嵌入式兼职\开发环境\host_tool_tests\test_net48_ui_contract.py`
  - 上位机 UI/.NET 4.8 契约测试。
- `D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester`
  - MQTT 5.0 联调工具。

## 四、已验证结果

最后一次验证结果：

- 固件构建通过：
  - 命令：`& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Build-LocalEMQX-Firmware.ps1' -Clean`
  - 输出关键值：`HEX_RANGE=0x00004000-0x00010863`
- 上位机构建通过：
  - 命令：`dotnet build 'D:\code\唐家湾嵌入式兼职\mqttv5_tool\mqttv5_tool\mqttv5_tool.csproj' -v:minimal`
  - 结果：`0 个警告，0 个错误`
- 静态测试通过：
  - 命令：`& 'D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester\.venv\Scripts\python.exe' -m unittest 'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_local_emqx_firmware_config.py' 'D:\code\唐家湾嵌入式兼职\开发环境\host_tool_tests\test_net48_ui_contract.py'`
  - 结果：`Ran 13 tests ... OK`
- ST-LINKV2 烧录通过：
  - 命令：`& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Flash-MQTTv5-STLink.ps1' -Image 'D:\code\唐家湾嵌入式兼职\mqttv5(1)\eide\build\LocalEMQX\mqttv5_local_emqx.hex'`
  - 结果：APP 回读一致。
  - bootloader `0x00000000-0x00003FFF` 回读确认未变化。
  - 烧录范围：`0x00004000 - 0x00012000`
- EMQX 联调通过：
  - EMQX：`D:\Tools\emqx-5.2.0`
  - 监听端口：`0.0.0.0:1883`
  - PC IP：`192.168.0.110`
  - 板子 IP：`192.168.0.111`
  - 客户端：`GM400-452089`
  - EMQX 显示：`subscriptions=5`，`connected=true`
  - 已收到 telemetry 上报：`{"source":"hc32f460_w5500","message":"hello from board"}`
- Modbus TCP 联调通过：
  - `192.168.0.111:502` 可连接。
  - 功能码 `03` 可读取 holding registers。

## 五、交付包

当前完整交付包：

- `D:\code\唐家湾嵌入式兼职\交付包\emqx_gateway_net48_20260628-002133.zip`

交付包内容：

- `source/mqttv5(1)`：固件源码。
- `source/mqttv5_tool`：.NET 4.8 上位机源码。
- `artifacts`：固件 hex/bin/elf/map、上位机 exe、NModbus.dll、烧录回读文件。
- `tools_and_tests`：构建脚本、烧录脚本、测试脚本、MQTT 联调工具。
- `docs`：方案、界面文档、需求资料、界面文档提取内容。
- `资料`：HC32F460 BSP/driver 等芯片资料。

## 六、后续建议

优先级从高到低：

1. 继续现场联调上位机 UI 的“查找/单机配置/批量配置/重启”完整流程。
2. 按客户实际 EMQX 服务器地址，通过上位机写入 host/port/topics/qos。
3. 如果最终必须启用 TLS1.2 双向认证，需要明确证书存储位置、证书大小、W5500 socket buffer 与 mbedTLS RAM 占用，再做单独分支；当前已验证版本是明文 MQTT 5.0 直连 EMQX 1883。
4. 若要做量产，建议给 `system_config_t` 增加版本号和 CRC 校验，避免后续结构变化导致旧配置误解析。
5. 上位机语言切换目前主要完成中文界面和语言入口，若要完整中英切换，需要补全资源化字符串。

## 七、重要注意事项

- 不要更新 ST-LINK 固件版本。OpenOCD 会提示“Consider updating your ST-Link firmware”，忽略即可。
- 不要使用旧的 `开发环境\scripts\hc32_openocd_flash_mqttv5.generated.tcl` 做烧录；该旧脚本包含从 `0x00000000` 写入的历史逻辑。
- 烧录必须使用 `Flash-MQTTv5-STLink.ps1`，该脚本会跳过 `0x4000` 以下数据并回读校验 bootloader 未变化。
- 当前目录不是 git 仓库，`git status` 会失败，不能依赖 git diff 查看改动。
- 当前板子 IP、EMQX 客户端 ID 可能因 DHCP/MAC 变化而改变，重新联调前先查 EMQX clients 和局域网 502。
