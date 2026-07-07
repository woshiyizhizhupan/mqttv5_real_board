# Real Board MQTT 5.0 Protocol And Test Report

日期：2026-06-28

## 工程位置

- 新真实板卡工程：`D:\code\唐家湾嵌入式兼职\mqttv5_real_board`
- 安全应用链接地址：`0x00004000`
- 构建脚本：`D:\code\唐家湾嵌入式兼职\开发环境\scripts\Build-RealBoard-Firmware.ps1`
- 构建产物：
  - `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\eide\build\LocalEMQX\mqttv5_real_board.hex`
  - `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\eide\build\LocalEMQX\mqttv5_real_board.bin`
  - `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\eide\build\LocalEMQX\mqttv5_real_board.elf`

## 上报协议

真实板卡工程继续使用当前 EMQX MQTT 5.0 五主题配置：

- telemetry up：`/local_pk/hc32f460_dev/user/telemetry/up`
- cmd down：`/local_pk/hc32f460_dev/user/cmd/down`
- event up：`/local_pk/hc32f460_dev/user/event/up`
- status up：`/local_pk/hc32f460_dev/user/status/up`
- debug up：`/local_pk/hc32f460_dev/user/debug/up`

telemetry schema 改为：

```json
{
  "schema": "emqx-gateway.realboard.telemetry.v1",
  "device_id": "GM400-xxxxxx",
  "business_mode": "real_board",
  "electrical": {},
  "lighting": {},
  "device": {},
  "peripherals": [],
  "factory_test": {},
  "legacy_protocol": {}
}
```

恢复的旧业务上报大类：

- `electrical`：电压、电流、有功功率、无功功率、功率因数、单次电量、累计电量、脉冲数。
- `lighting`：灯 1/灯 2 亮度、继电器 1/2、开灯时间、本次亮灯时间、累计亮灯时间。
- `device`：地址、心跳周期、板载温度、错误码、错误更新标志、运行 tick。
- `peripherals`：灯、环境、充电 C2、充电 C3、倾斜、LORA、GPS 七类旧外设通道。
- `factory_test`：测试负载功率、设备类型、注册标志、测试次数、测试结果、测试使能。
- `legacy_protocol`：旧协议下发支持状态、旧 ProductKey/DeviceName 运行态、最近外设命令。

## 下发协议

保留现有标准 JSON 命令：

- `ping`
- `get_status`
- `get_config`
- `set_config`
- `reboot`

新增旧协议兼容命令：

```json
{
  "id": "cmd-001",
  "cmd": "legacy_frame",
  "frame": "<base64 legacy frame>"
}
```

也兼容旧平台常见嵌套字段：

- `frame`
- `params.frame`
- `params.params`
- `data.frame`

旧二进制帧格式：

```text
0xFE | frm_type | cmd_type | payload_len_hi | payload_len_lo | payload | crc32_be
```

CRC 使用旧工程 `crc32()` 等价算法，计算范围为去掉前导 `0xFE` 后的 `frm_type` 到 payload 末尾。

已接入的旧命令：

- `0xA1/0x09`：读 CPU ID，返回基于 `device_id + MAC` 的 12 字节兼容 ID。
- `0xA1/0x0A`：设置旧 ProductKey，保存在运行态。
- `0xA1/0x0B`：设置旧 DeviceName，映射到当前 `device_id` 并保存配置，需重启。
- `0xA1/0x0C`：设置旧 DeviceSecret，保存在运行态。
- `0xA1/0xFA`：设置 MQTT host/port，payload 为 `host,port`，保存配置并重启。
- `0xA1/0xFB`：旧 ProductKey/DeviceName/DeviceSecret 组合命令，当前保存在运行态。
- `0xA2/0x01`：复位。
- `0xA2/0x02`：全设备电源控制。
- `0xA2/0x03`：灯电源控制。
- `0xA2/0x0D`：继电器 1 控制。
- `0xA2/0x0E`：继电器 2 控制。
- `0xA5/0x01-0x07`：七类外设下发入口已接收并记录；底层 UART/RS485 透传驱动待补齐后可接到此入口。

## 测试结果

静态契约测试：

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\host_mqtt_tester\.venv\Scripts\python.exe' -m unittest `
  'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_local_emqx_firmware_config.py' `
  'D:\code\唐家湾嵌入式兼职\开发环境\firmware_tests\test_real_board_business_contract.py' `
  'D:\code\唐家湾嵌入式兼职\开发环境\host_tool_tests\test_net48_ui_contract.py'
```

结果：`25 tests OK`

真实板卡工程构建：

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Build-RealBoard-Firmware.ps1' -Clean
```

结果：

- 构建成功。
- `HEX_RANGE=0x00004000-0x00017067`
- Flash 使用：`72160 B / 496 KB`
- RAM 使用：`14732 B / 188 KB`

原 LocalEMQX 工程回归构建：

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Build-LocalEMQX-Firmware.ps1' -Clean
```

结果：

- 构建成功。
- `HEX_RANGE=0x00004000-0x000157D7`

## 当前限制

`资料` 目录中旧工程引用过 `MeterHLW8112.c`、环境采集、RS485 外设模块，但源码快照不完整。当前新工程已经恢复协议层、上报结构和下发入口；真实电表/环境/RS485 采样值暂由 `real_board_business.c` 的板卡业务状态承载，后续补齐驱动时只需要替换采样更新逻辑。
