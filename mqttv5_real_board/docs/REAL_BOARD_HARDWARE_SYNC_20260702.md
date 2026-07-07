# 真实板卡硬件接入对齐记录 2026-07-02

## 参考旧工程

- `D:\code\唐家湾嵌入式兼职\MQTT_ali_app\user\source\main.c`
  - `HardWare_Init()`：GPIO、UART、W5500、OTS、IIC、PWM 初始化。
  - `Lamp_Handle_Main()`：电表初始化、告警、上报、环境采样、用电保存。
  - 主循环：`Uart_Loop_queue_handle()`、`Lamp_Handle_Main()`、`OutDevice_TimeOutHandle()`、`MeterHLW8112_ReadRegData_Handle()`。
- `D:\code\唐家湾嵌入式兼职\MQTT_ali_app\user\source\Uart.h/.c`
  - USART1：PB00/PB01，115200。
  - USART2：PA02/PA03，4800，用于 RS485 外设。
  - USART3：PB12/PB10，9600 Even parity，用于 HLW8112。
- `D:\code\唐家湾嵌入式兼职\MQTT_ali_app\user\source\MeterHLW8112.c/.h`
  - 电表初始化：RST、Enable、IA、EMUCON、EMUCON2、Disable。
  - 周期读取：Ufreq、RmsIA/RmsIB/RmsU、PowerFactor、EnergyPA/EnergyPB、PowerPA/PowerPB/PowerS 和校准寄存器。
- `D:\code\唐家湾嵌入式兼职\MQTT_ali_app\user\user\RS485_Peripheral.c`
  - 温湿度传感器查询帧：`01 04 00 01 00 02 CRC16`。
  - RS485 发送方向脚：PC13；RS485 使能脚：PH02。

## 本次真实固件对齐内容

- `driver/usart.c/.h`
  - 恢复 USART1 到 PB00/PB01，避免占用旧板卡 PA04/PA05 的 `GPIO_IN/LED1`。
  - 增加 USART2、USART3 初始化、发送、接收缓存和 `read_available` 接口。
  - USART2 按旧工程配置为 4800，无校验，用于 RS485。
  - USART3 按旧工程配置为 9600，偶校验，用于 HLW8112。
- `src/real_board_hardware.c/.h`
  - 新增真实硬件采集任务 `real_board_hardware_task()`。
  - 恢复 HLW8112 初始化序列、寄存器读取序列和旧工程计算公式，并写入 `real_board_business_update_meter_hlw8112()`。
  - HLW8112 接收改为分片累计后校验，校验成功才推进寄存器序号；5 秒无响应会清空接收缓存并重新打开读取，避免串口无返回时卡住采样周期。
  - 周期发送温湿度 RS485 查询帧，收到合法 Modbus 响应后写入 `real_board_business_update_environment()`。
  - 维护 RS485 在线、收发计数、错误计数，并写入 `real_board_business_update_rs485()`。
  - 提供 `real_board_rs485_send()`，供 A5 外设透传走真实 UART2/RS485。
  - RS485/A5 外设恢复旧工程同步等待模型的核心行为：4 槽发送队列、active 命令类型记录、1 秒响应超时、环境轮询与 A5 透传共用队列，避免互相抢占。
- `src/real_board_business.c`
  - A5 pending 逻辑保留，同时通过弱符号钩子调用真实硬件透传；模拟版本无硬件实现时不受影响。
  - A5 下发在同命令已有 pending、pending 槽满、硬件队列满时返回 busy，响应 payload 使用旧协议 `FRM_MOD_BUSY(0x07)` 语义。
  - RS485 回包按 active A5 `cmd_type` 回填，不再固定写入 `0x02`。
- `src/main.c`
  - 初始化时调用 `real_board_hardware_init()`。
  - 主循环中在业务 tick 前调用 `real_board_hardware_task()`，保证 MQTT 上报读取最近硬件状态。

## 已验证

- 固件契约测试：`33 tests OK`。
- 真实固件编译通过：FLASH 使用 `178616 B / 248 KB`，RAM 使用 `38736 B / 188 KB`。
- 固件输出：
  - HEX：`D:\code\唐家湾嵌入式兼职\mqttv5_real_board\eide\build\LocalEMQX\mqttv5_real_board.hex`
  - BIN：`D:\code\唐家湾嵌入式兼职\mqttv5_real_board\eide\build\LocalEMQX\mqttv5_real_board.bin`
  - 地址范围：`0x00004000-0x0003166B`

## 现场验证要点

- HLW8112 配置已按旧工程接入，现场只需确认实际串口电气和返回 CRC 稳定性。
- RS485 温湿度查询已固定为旧配置：地址 `0x01`，功能码 `0x04`，起始寄存器 `0x0001`，读取两个寄存器。
- A5 外设已实现真实 UART2/RS485 队列透传和响应回填；若后续确认 UART4/C3 物理通道也接在当前板卡上，再单独补 USART4 物理通道。
