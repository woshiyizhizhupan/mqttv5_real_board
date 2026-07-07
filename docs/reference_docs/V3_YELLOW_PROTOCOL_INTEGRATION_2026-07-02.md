# V3 标黄业务协议接入记录

日期：2026-07-02

## 接入范围

本次基于 `业务层协议V3(1).xlsx` 的标黄项，同步接入：

- 真实固件：`mqttv5_real_board`
- 模拟固件：`mqttv5_sim_board`

## 已接入功能

### A1 主板参数类

以下旧协议命令继续兼容：

- `0x09 / 0x89`：读取 MCU UUID，当前按旧兼容逻辑返回由 `device_id + MAC` 生成的识别数据。
- `0x0A / 0x8A`：设置产品 Key，映射到运行态 legacy product key。
- `0x0B / 0x8B`：设置产品名/设备名，映射到 `device_id` 并保存配置。
- `0x0C / 0x8C`：设置设备秘钥，映射到运行态 legacy device secret。

### A5 外设子帧

A5 外层继续使用旧业务帧，payload 内支持 V3 子格式：

```text
FE <sub_cmd> <len_hi> <len_lo> <payload...> <crc32>
```

已接入子命令：

- `0x01`：灯控，地址 + 亮度，返回地址 + 状态。
- `0x02`：读取遥测/心跳数据，返回地址、时间、温度、亮度、电压、电流、功率、功率因数、电量、故障码。
- `0x10`：读取版本，返回地址 + 硬件版本 + 软件版本。
- `0x11`：读取上报间隔，返回地址 + 秒数。
- `0x14`：读取定时任务参数，返回地址 + 简化定时参数。
- `0x15`：读取出厂序列号，返回地址 + 由 `device_id` 生成的 4 字节序列号。
- `0x20`：设置地址。
- `0x21`：设置上报间隔。
- `0x22`：设置功率常数，当前映射到测试功率参数承载字段。
- `0x23`：设置定时控制参数，当前映射到灯亮度/继电器状态承载字段。
- `0x25`：恢复默认参数。
- `0x26`：复位。
- `0x28`：设置 RTC 时间，当前接收并返回状态。
- `0x29`：清除故障。
- `0x2A`：兼容阈值/参数设置，当前映射到测试功率参数承载字段。

### OTA

真实固件和模拟固件均支持：

- A5 嵌套 OTA：`0xB0` 至 `0xB6`。
- A4 旧升级帧入口。
- B1 文件信息同时兼容两种字段顺序：
  - `file_len + chunk_count + chunk_size + crc`
  - `file_len + chunk_size + chunk_count + crc`

模拟固件新增 payload 级 OTA 回包接口，使 A5 嵌套 OTA 返回 A5/81 外层格式，不再只返回 A4 直接帧。

### MQTT payload 外层

模拟固件补齐与真实固件一致的 MQTT 包装兼容：

请求：

```json
{"payload":"FEA501000BFE010003FFFF502EFC45833F1E03D2"}
```

响应：

```json
{"payload":"FEA581..."}
```

同时保留 `frame/msg` 的 Base64 字段和 `frame_hex/msg_hex` 的十六进制字段，便于不同服务端封装方式取用。

## 修改文件

- `mqttv5_real_board/src/real_board_business.c`
- `mqttv5_real_board/src/real_board_ota.c`
- `mqttv5_sim_board/src/sim_board_business.c`
- `mqttv5_sim_board/src/sim_board_ota.c`
- `mqttv5_sim_board/src/sim_board_ota.h`
- `mqttv5_sim_board/src/main.c`

## 构建与验证

构建结果：

- 真实固件构建通过，HEX 范围：`0x00004000-0x0002F743`
- 模拟固件构建通过，HEX 范围：`0x00004000-0x0001AD27`

静态测试：

- `39 tests OK`
- 真实/模拟业务层均检查到 V3 标黄 A5 子命令字面量。

生成文件：

- `D:\code\唐家湾嵌入式兼职\mqttv5_real_board\eide\build\LocalEMQX\mqttv5_real_board.hex`
- `D:\code\唐家湾嵌入式兼职\mqttv5_sim_board\eide\build\LocalEMQX\mqttv5_sim_board.hex`

## 烧录记录

已向当前连接板卡烧录模拟固件：

```powershell
& 'D:\code\唐家湾嵌入式兼职\开发环境\scripts\Flash-MQTTv5-STLink.ps1' -Image 'D:\code\唐家湾嵌入式兼职\mqttv5_sim_board\eide\build\LocalEMQX\mqttv5_sim_board.hex'
```

烧录结果：

- 写入范围：`0x00004000 - 0x0001C000`
- 写入大小：`98304 bytes`
- APP 校验：通过
- bootloader 区域：`0x00000000-0x00003FFF` 前后一致，未覆盖

脚本输出关键结论：

```text
APP verify passed at 0x00004000; bootloader region 0x00000000-0x00003FFF unchanged.
```

烧录后已额外执行一次非擦写 `reset run`。

