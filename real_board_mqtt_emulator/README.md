# Real Board MQTT Emulator

Independent PC-side MQTT 5.0 emulator for `mqttv5_real_board`.

It connects to EMQX as `GM400-452089`, subscribes to the real board command and OTA topics, publishes real-board telemetry/status/event/debug/response schemas, and generates local simulated meter, environment, RS485, lighting, and alarm values.

## Install

```powershell
cd 'D:\code\唐家湾嵌入式兼职\real_board_mqtt_emulator'
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

## Run Against Remote EMQX

```powershell
& 'D:\code\唐家湾嵌入式兼职\real_board_mqtt_emulator\run_emulator.ps1' -Username 'GM400-452089' -Password public -LogDir 'D:\code\唐家湾嵌入式兼职\real_board_mqtt_emulator\logs'
```

Default MQTT contract:

- Broker: `39.103.154.108:1883`
- Client ID: `GM400-452089`
- Username/password for the current remote EMQX test instance: `GM400-452089` / `public`
- QoS: `2`
- Telemetry: `city/tjw/pole/pole001/device/GM400-452089/`
- Command: `city/tjw/pole/pole001/device/GM400-452089/get`
- Event/response/status: `city/tjw/pole/pole001/device/GM400-452089/event`
- OTA: `city/tjw/pole/pole001/device/GM400-452089/ota`
- Debug: `city/tjw/pole/pole001/device/GM400-452089/debug`

## Short Smoke Run

```powershell
& 'D:\code\唐家湾嵌入式兼职\real_board_mqtt_emulator\run_emulator.ps1' -Username 'GM400-452089' -Password public -MaxRuntime 20 -LogDir 'D:\code\唐家湾嵌入式兼职\real_board_mqtt_emulator\logs'
```

The emulator process only opens MQTT client connections. It does not modify firmware, flash scripts, or bootloader/application images.
