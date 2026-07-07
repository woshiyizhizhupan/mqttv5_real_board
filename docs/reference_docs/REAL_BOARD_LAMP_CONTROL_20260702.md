# 真实板卡灯具控制整理

日期：2026-07-02

## 旧业务依据

- 上电硬件初始化：`D:\code\唐家湾嵌入式兼职\MQTT_ali_app\user\source\main.c:234` 的 `gpio_init(void)`。
- 旧上电流程：`D:\code\唐家湾嵌入式兼职\MQTT_ali_app\user\source\main.c:370` 的 `HardWare_Init(void)` 会先执行 `gpio_init();`，随后再初始化 W5500、PWM 等外设。
- 旧灯具动作：`D:\code\唐家湾嵌入式兼职\MQTT_ali_app\user\user\PWM.c:122` 的 `Adjust_UNIT4_Lamp(uint8_t bright)` 和 `D:\code\唐家湾嵌入式兼职\MQTT_ali_app\user\user\PWM.c:242` 的 `Adjust_Lamp(uint8_t bright)`。
- 旧引脚定义：`D:\code\唐家湾嵌入式兼职\MQTT_ali_app\user\user\platform_config.h`。

## 关键引脚

| 功能 | 旧工程定义 | 当前真实固件保持 |
| --- | --- | --- |
| W5500 reset | PA10 / `RESET_W5500` | PA10 / `REAL_BOARD_RESET_W5500` |
| 一路继电器 | PA12 / `SWLED1` | PA12 / `REAL_BOARD_SWLED1` |
| 二路继电器 | PC15 / `SWLED2` | PC15 / `REAL_BOARD_SWLED2` |
| 一路 PWM | PA00 / TimerA2 CH1 | PA00 / `CM_TMRA_2` `TMRA_CH1` |
| 二路 PWM | PC14 / TimerA4 CH5 | PC14 / `CM_TMRA_4` `TMRA_CH5` |
| RS485 enable | PH02 / `RS485_EN` | PH02 / `REAL_BOARD_RS485_EN` |

## 0x01 控灯动作

外层 A5，内层功能码 0x01：

- 继电器吸合示例：`FE A5 01 00 0B FE 01 00 03 FF FF 64 17 A7 8E 6F 07 C7 CB 38`
- 继电器断开示例：`FE A5 01 00 0B FE 01 00 03 FF FF 00 5D 78 2B 2E 50 07 A5 24`

地址含义：

- `0001`：一路灯具。
- `0002`：二路灯具。
- `FFFF`：广播，两路灯具同时执行。

亮度含义：

- `00`：继电器断开，并按旧算法更新 PWM。
- `01` 到 `64`：继电器吸合，并按旧算法更新 PWM。
- 大于 `64` 的值在业务层钳制为 `64`。

## 当前实现位置

- 新增真实硬件层：`D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\real_board_lamp.c`
- 对外保留旧函数名：`gpio_init()`、`PWM_Init()`、`PWM_UNIT4_Init()`、`Adjust_Lamp()`、`Adjust_UNIT4_Lamp()`。
- 上电调用：`D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\main.c` 中 `real_board_lamp_init();` 放在 `w5500_init();` 之前。
- 0x01 业务入口：`D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\real_board_business.c` 的 `real_board_set_lamp_brightness()`。

## 注意点

- 旧 `gpio_init()` 会先把 PA10 W5500 reset 拉低；当前真实固件随后执行 `w5500_init()`，由 W5500 平台层重新释放复位脚，避免网口保持复位。
- 当前保持旧业务的继电器和 PWM 引脚，不重新分配板卡 IO。
- TimerA LL 驱动开关已打开：`D:\code\唐家湾嵌入式兼职\mqttv5_real_board\src\hc32f4xx_conf.h` 的 `LL_TMRA_ENABLE`。
