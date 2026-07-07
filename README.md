# EMQX Gateway Multi-Topic Pure JSON Source

This repository layout is prepared from `multi_topic_pure_json_full_source_20260703`.

## Layout

- `mqttv5_real_board/`: HC32F460 + W5500 real-board firmware restored from the 2026-07-03 source package.
- `mqttv5_sim_board/`: simulated-board firmware/support target required by the static contract tests.
- `mqttv5_tool/`: .NET Framework 4.8 WinForms host tool, with the project under `mqttv5_tool/mqttv5_tool`.
- `emqx_device_emulator_tool/`: EMQX device emulator used by host-tool contract checks.
- `real_board_mqtt_emulator/`: real-board MQTT emulator/support tool.
- `开发环境/`: firmware tests, host-tool tests, probes, MQTT tester source, flash dependencies, and scripts from the 2026-07-03 package.
- `docs/`: protocol discussion and reference handoff documents from the 2026-07-03 package.

## Safety

- Do not erase the full MCU flash.
- Do not write `0x00000000-0x00003FFF`.
- Do not use `开发环境/scripts/hc32_openocd_flash_mqttv5.generated.tcl`.
- Do not update ST-LINK firmware.

The local rollback source includes `mqttv5_real_board/src/real_board_tls_certs.c`, which contains a TLS client private key literal. It is ignored by default for Git publishing. Replace it with a non-secret template or inject certificates from a private channel before building a public or shared repository copy.
