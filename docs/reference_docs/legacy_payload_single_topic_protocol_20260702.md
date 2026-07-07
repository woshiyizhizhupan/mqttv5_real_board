# 单主题 MQTT + 旧业务帧兼容协议说明

日期：2026-07-02

适用工程：

- 真实板卡固件：`mqttv5_real_board`
- 模拟固件低风险同步项：`mqttv5_sim_board`
- 上位机/配置工具：`mqttv5_tool`、`开发环境/host_tool_config_probe`
- 旧业务参考工程：`MQTT_ali_app`
- 协议表参考：`业务层协议V2(1).xlsx`

## 1. 单主题规则

当前按“单主题”接入 EMQX：

```text
v1/devices/request/{device_name}
```

`device_name` 默认为设备类型名加 6 位设备 ID。当前固件默认生成：

```text
GM400-<MAC后三字节大写HEX>
示例：GM400-452089
默认主题：v1/devices/request/GM400-452089
```

说明：

- `device_id` 仍然可以通过上位机或旧 A1-0x0B 指令修改。
- 五个业务 topic 槽位仍保留，作为固件内部“遥测、下行、事件、OTA、调试”索引使用。
- 新默认值会把五个槽位都写成同一个完整主题。
- 旧通配订阅 `v1/devices/request/+` 仍可通过上位机手动配置，但这次默认配置和一键配置按单主题精确 topic 执行。

## 2. MQTT JSON 包装

### 2.1 下发支持格式

真实板卡当前支持以下下发 JSON：

```json
{"payload":"FEA501000BFE010003FFFF502EFC45833F1E03D2"}
```

```json
{
  "connectType": "1",
  "msgType": "1",
  "payload": "FEA501000BFE010003FFFF502EFC45833F1E03D2",
  "timestamp": "1782916895695"
}
```

```json
{
  "mutable": true,
  "payload": "FEA501000BFE010003FFFF502EFC45833F1E03D2",
  "qos": 1,
  "retained": false,
  "dup": false,
  "messageId": 12345,
  "properties": {
    "property1": "value1",
    "property2": 42
  }
}
```

旧阿里风格也保留：

```json
{"id":"xxx","params":{"frame":"<Base64业务帧>"}}
```

兼容取值位置：

- 顶层：`payload`、`frame`、`msg`
- `params` 内：`payload`、`frame`、`msg`、`params`
- `data` 内：`payload`、`frame`、`msg`

真实板卡对业务帧文本的解码顺序：

1. 如果看起来是十六进制，按 hex 解码。
2. 否则先尝试 Base64。
3. Base64 失败后再按 hex 兜底。

### 2.2 响应格式

如果下发是 `payload` 包装，真实板卡响应仍返回 `payload`：

```json
{
  "mutable": true,
  "qos": 1,
  "retained": false,
  "dup": false,
  "messageId": 12345,
  "properties": {
    "property1": "value1",
    "property2": 42
  },
  "connectType": "1",
  "msgType": "1",
  "payload": "FEA581000100D67E2E77",
  "timestamp": "0"
}
```

说明：

- 响应 topic 走同一个单主题。
- `payload` 是十六进制业务响应帧。
- `mutable/qos/retained/dup/messageId/properties/id` 会尽量从请求复制。
- 如果请求没有 `connectType/msgType/timestamp`，固件默认补 `1/1/0`。
- 单主题下设备也会收到自己发布的响应。真实板卡接收入口会识别并忽略 `payload` 以 `FE A5 81` 开头的旧同步响应帧，避免响应帧再次进入 A5 下行业务造成重复回包。

如果下发是新 JSON 命令，例如 `{"id":"1","cmd":"ping"}`，响应仍走当前 JSON：

```json
{
  "schema": "emqx-gateway.response.v1",
  "id": "1",
  "device_id": "GM400-452089",
  "cmd": "ping",
  "ok": true,
  "code": 0,
  "message": "ok",
  "data": {"pong": true}
}
```

## 3. 总帧 A

V2 表和旧工程总帧格式：

| 字段 | 长度 | 说明 |
|---|---:|---|
| 前导码 | 1 | `0xFE`，用于 MQTT 同步/异步业务帧识别 |
| 功能码A / 帧类型A | 1 | `0xA1`/`0xA2`/`0xA3`/`0xA4`/`0xA5`/`0xA6` |
| 命令类型A | 1 | 业务命令 |
| 负荷长度A | 2 | 大端，不包含 CRC |
| 负荷数据A | N | 对 A5 通常是子帧 B |
| CRC32 A | 4 | 不包含前导码 `FE`，覆盖 `[功能码A, 命令类型A, 长度A, 数据A]` |

示例，V2 表中的灯控查询版本：

```text
FE A5 01 00 08 FE 10 00 02 FF FF 1B60A60E B70A16B4
```

当前真实板卡解析：

- 允许多个连续 `FE` 前导码。
- 长度字段按大端解析。
- CRC32 同时兼容大端和小端尾部存储。

## 4. 旧工程 MQTT 分发逻辑

旧 `MQTT_ali_app/user/user/MQTT_App.c` 实际路径：

1. MQTT 收到 `PUBLISH`。
2. 从 JSON 里取 `frame` 并 Base64 解码。
3. `Protocol_Frm_Decode` 解析总帧 A。
4. 如果 `frm_type == 0xA6`，直接重新封装回包。
5. 否则提取 JSON 的 `id`，并把 topic 里的 `request` 替换为 `response`。
6. 分发：
   - `0xA1` -> `Protocol_Factory_Set`
   - `0xA2` -> `Protocol_A2_Ctr`
   - `0xA5` -> 外设/环境/灯控/OTA

旧同步响应最终由 `SynHandleUPSend(... Port=0x01)` 包成：

```text
FE A5 81 <len> <payload> <crc32>
```

当前真实板卡为了兼容旧服务器伪同步逻辑，也按 `FE A5 81 ...` 生成同步响应帧。

## 5. A1 主板参数类

V2 表定义：

| A 命令 | V2 含义 | 旧 MQTT_ali_app 实际处理 | 当前真实板卡兼容 |
|---|---|---|---|
| `0x01`-`0x08` | 早期参数/版本/时间/串口/网络模式 | 旧当前代码未走这些分支 | 返回 received |
| `0x09` | 读 MCU UUID，转设备 ID | `Read_CpuId_Decode` | 返回设备 ID/MAC 派生信息 |
| `0x0A` | 设置 ProductKey | `Set_Key_Decode` | 保存到运行态 `legacy_product_key` |
| `0x0B` | 设置产品名/设备名 | `Set_UserName_Decode` | 映射到 `device_id` 并保存，需重启生效 |
| `0x0C` | 设置 DeviceSecret | `Set_Secret_Decode` | 保存到运行态 `legacy_device_secret` |
| `0x0D` | 4G 模块透传 | V2 表有定义，旧当前代码未处理 | 返回 received |
| `0xFA` | 设置服务器地址 | `Set_IP_Decode` | 解析 `host,port` 并保存 MQTT host/port |
| `0xFB` | 设置 KNS/三元组组合 | `Set_KNS_Decode` | 接收并保存兼容状态 |

## 6. A2 主板控制类

V2 表定义：

| A 命令 | 含义 | 当前真实板卡兼容 |
|---|---|---|
| `0x01` | 主机复位 | 接收后计划重启，回状态 `0x00` |
| `0x02` | 外设所有设备电源开关 | 设置 relay1/relay2 |
| `0x03` | 灯控电源开关 | 设置 lamp1/relay1 |
| `0x04` | 环境设备电源开关 | 返回 received |
| `0x05` | 充电桩 C2 电源开关 | 返回 received |
| `0x06` | 充电桩 C3 电源开关 | 返回 received |
| `0x07` | 倾斜仪电源开关 | 返回 received |
| `0x08` | 摄像头电源开关 | 返回 received |
| `0x09` | LED 显示电源开关 | 返回 received |
| `0x0A` | AP 电源开关 | 返回 received |
| `0x0B` | 一键求助电源开关 | 返回 received |
| `0x0C` | 广播设备电源开关 | 返回 received |
| `0x0D` | 继电器 1 电源开关 | 设置 relay1 |
| `0x0E` | 继电器 2 电源开关 | 设置 relay2 |
| `0x0F` | 空/预留 | 返回 received |

返回状态沿用旧定义：

| 状态 | 含义 |
|---|---|
| `0x00` | 成功 |
| `0x01` | 校验码错误 |
| `0x02` | 长度错误 |
| `0xFF` | 其它错误 |

## 7. A3/A4

| 帧类型 | V2 含义 | 当前兼容 |
|---|---|---|
| `0xA3` | 查询/异常，未使用 | 返回 received |
| `0xA4` | 保留/升级 | 返回 received；实际 OTA 走 A5 内嵌 B0-B6 |

## 8. A5 外设类

V2/旧头文件定义：

| A5 命令 | 外设 |
|---|---|
| `0x01` | 灯控 |
| `0x02` | 环境 |
| `0x03` | 充电桩 C2 |
| `0x04` | 充电桩 C3 |
| `0x05` | 倾斜仪 |
| `0x06` | LORA |
| `0x07` | GPS |

当前真实板卡处理策略：

- 如果 A5 payload 以 `FE` 开头，按子帧 B 解析。
- 如果不是子帧 B，进入 A5 外设透传/等待队列。
- 等待队列最大 4 条，默认超时 300 tick。
- 有真实 RS485/外设响应时可通过 `real_board_business_handle_peripheral_response` 回填。

## 9. 子帧 B

V2 表子帧格式：

| 字段 | 长度 | 说明 |
|---|---:|---|
| 前导码 | 1 | `0xFE` |
| 功能码B | 1 | 灯控/查询/OTA 命令 |
| 负荷长度B | 2 | 大端，不包含 CRC |
| 目标地址 | 2 | 多数灯控/外设命令使用 |
| 数据B | N | 命令数据 |
| CRC32 B | 4 | 不包含前导码 |

V2 表列出的灯控/外设查询：

| B 命令 | 下发含义 | 响应含义 |
|---|---|---|
| `0x10` | 查询版本 | 返回版本号 |
| `0x11` | 查询采集数据上报间隔时间 | 返回上报间隔秒 |
| `0x12` | 查询配置常数 | 返回配置常数 |
| `0x13` | 查询扩展功能，未使用 | 返回扩展功能，未使用 |
| `0x14` | 查询开关定时任务参数 | 返回定时开关参数 |
| `0x15` | 查询出厂设置串号 | 返回出厂设置串号 |
| `0x16` | 预留 | 返回人体感应各项数值 |

当前真实板卡明确实装的子帧：

| B 命令 | payload | 当前行为 |
|---|---|---|
| `0x01` | `目标地址2字节 + 亮度1字节` | 灯控亮度设置，地址 `0x0001` 灯1、`0x0002` 灯2、`0xFFFF` 双灯；亮度裁剪到 0-100 |
| `0xB0`-`0xB6` | OTA payload | 进入旧 OTA 兼容流程 |
| 其它 | 任意 | 返回 nested received |

## 10. OTA B0-B6

OTA 总体封装：A5 外层 + B 子帧。

```text
FE A5 01 <lenA> FE Bx <lenB> <payloadB> <crcB> <crcA>
```

当前真实板卡 OTA 分区：

| 名称 | 地址 |
|---|---|
| APP1 | `0x00004000` |
| APP2 | `0x00042000` |
| APP2 end | `0x00076000` |
| Upgrade state | `0x0007A000` |

OTA 命令：

| B 命令 | 下发 payload | 响应 payload | 当前行为 |
|---|---|---|---|
| `0xB0` | 空 | 7 字节：硬件主/次、软件主/次、`xm`、`0x01` | 查询/开始升级信息 |
| `0xB1` | `file_len(4) + chunk_count(2) + chunk_size(2) + file_crc32(4)` | 1 字节：`0x01` 成功，`0x00` 失败 | 建立 OTA 会话 |
| `0xB2` | `chunk_index(2) + chunk_data(N)` | 4 字节：原 index + `0x01 0x00` | 写入 APP2；当前支持 512 字节分包 |
| `0xB3` | 空 | `missing_count(2) + verify(1) + missing_index...` | 查询丢包；全收齐后校验 CRC，成功写升级标志 |
| `0xB4` | 空 | 1 字节：`0x01` 可结束，`0x00` 未完成 | 完成升级；可按编译选项触发重启 |
| `0xB5` | 空 | 4 字节版本号 | 查询软件/硬件版本 |
| `0xB6` | 空 | 1 字节：`0x01` | 中断升级 |

512 字节分包说明：

- `chunk_size` 可为 512。
- `B2` 实际 payload 长度为 `2 + chunk_data_len`。
- 最大 chunk 数由 `REAL_BOARD_OTA_MAX_CHUNKS` 和 APP2 分区共同约束。
- 固件用 bitset 记录已收分包，避免 512 包时占用过多 RAM。

## 11. 异步上报

旧工程异步上报通过 `AsyHandleUPSend` 包装，典型外层仍为 `FE A5 (0x80|Port)`。

旧工程中可见的异步帧：

| 帧 | 来源 | 含义 |
|---|---|---|
| `F0 40` | `Lamp_KeepAlive_Data_Time_Handle` | 灯具心跳/遥测：RTC、温度、亮度、电压、电流、功率、电量、亮灯时长等 |
| `F0 41` | `RTC_Data_Time_Upgrade_Req` | RTC 校时请求 |
| `F0 42` | `Error_Report_Fun` | 故障/恢复告警 |

当前真实板卡同时保留 JSON 遥测上报：

| JSON 字段 | 含义 |
|---|---|
| `meter_hlw8112` | 电表数据：电压、电流、有功/无功功率、功率因数、电量、脉冲 |
| `environment` | 环境数据：温度、湿度、PM2.5、CO2、照度 |
| `rs485` | RS485 外设状态：在线、设备数、周期、收发/错误计数 |
| `lighting` | 灯控/继电器状态 |
| `device` | 地址、心跳周期、错误码、运行 tick |
| `peripherals` | A5 外设配置/最近下发状态 |
| `factory_test` | 出厂/测试状态 |
| `legacy_protocol` | 旧协议兼容状态 |

## 12. 代码位置索引

旧工程：

- MQTT JSON 取帧/分发：`MQTT_ali_app/user/user/MQTT_App.c`
- 总帧/子帧解析：`MQTT_ali_app/user/user/protocol.c`
- A1/A2 实际分支：`MQTT_ali_app/user/user/protocol.c`
- 灯控/心跳/故障上报：`MQTT_ali_app/user/user/TimeApp.c`、`MQTT_ali_app/user/user/protocol.c`
- 环境/RS485 外设同步：`MQTT_ali_app/user/user/Meteorological.c`
- OTA：`MQTT_ali_app/user/user/upgrade.c`

当前真实板卡：

- 单主题默认：`mqttv5_real_board/src/config.c`
- MQTT JSON 包装/响应：`mqttv5_real_board/src/main.c`
- 旧 A1/A2/A5/子帧兼容：`mqttv5_real_board/src/real_board_business.c`
- OTA B0-B6：`mqttv5_real_board/src/real_board_ota.c`
