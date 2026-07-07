import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SIM_PROJECT = ROOT / "mqttv5_sim_board"
SIM_SRC = SIM_PROJECT / "src"


class SimBoardOtaContractTests(unittest.TestCase):
    def test_sim_board_project_is_isolated_from_real_board_project(self):
        self.assertTrue(SIM_PROJECT.exists(), "mqttv5_sim_board project folder must exist")
        self.assertNotEqual(SIM_PROJECT, ROOT / "mqttv5_real_board")
        self.assertTrue((SIM_PROJECT / "ldscripts" / "hc32f460_app_0x4000.lds").exists())

    def test_sim_board_build_script_targets_sim_project(self):
        build_ps1 = ROOT / "开发环境" / "scripts" / "Build-SimBoard-Firmware.ps1"
        params = SIM_PROJECT / "eide" / "build" / "Debug" / "builder.params"

        self.assertTrue(build_ps1.exists(), "sim board build script must exist")
        self.assertTrue(params.exists(), "sim board builder.params must exist")
        ps1_text = build_ps1.read_text(encoding="utf-8")
        params_text = params.read_text(encoding="utf-8")

        self.assertIn("mqttv5_sim_board", ps1_text)
        self.assertIn("mqttv5_sim_board", ps1_text)
        self.assertIn("../src/sim_board_business.c", params_text)
        self.assertIn("../src/sim_board_ota.c", params_text)
        self.assertNotIn("../src/real_board_business.c", params_text)

    def test_sim_board_keeps_safe_app_link_address(self):
        linker_script = SIM_PROJECT / "ldscripts" / "hc32f460_app_0x4000.lds"
        text = linker_script.read_text(encoding="utf-8")

        self.assertRegex(text, r"FLASH\s+\(rx\):\s+ORIGIN\s+=\s+0x00004000")
        self.assertRegex(text, r"LENGTH\s+=\s+248K")

    def test_ota_partition_matches_legacy_full_switch_contract(self):
        ota_h = (SIM_SRC / "sim_board_ota.h").read_text(encoding="utf-8")
        ota_c = (SIM_SRC / "sim_board_ota.c").read_text(encoding="utf-8")
        combined = ota_h + "\n" + ota_c

        for required in (
            "#define SIM_BOARD_FLASH_WRITE_END_ADDR 0x00080000UL",
            "#define SIM_BOARD_FLASH_PAGE_SIZE 0x00002000UL",
            "#define SIM_BOARD_APP1_START_ADDR 0x00004000UL",
            "#define SIM_BOARD_APP2_START_ADDR 0x00042000UL",
            "#define SIM_BOARD_APP2_END_ADDR 0x00076000UL",
            "#define SIM_BOARD_UPGRADE_STATE_ADDR 0x0007A000UL",
            "#define SIM_BOARD_UPGRADE_STATE_SUCCESS 0xA55AU",
            "#define SIM_BOARD_OTA_PACK_FLAG_BYTES 128U",
            "#define SIM_BOARD_OTA_MAX_CHUNKS 1024U",
        ):
            self.assertIn(required, combined)

        self.assertIn("sim_board_ota_legacy_handle_t", combined)
        self.assertIn("pack_flag[SIM_BOARD_OTA_PACK_FLAG_BYTES]", combined)
        self.assertIn("flash_erase_sector(SIM_BOARD_UPGRADE_STATE_ADDR)", ota_c)

    def test_ota_rejects_unsafe_ranges_and_uses_word_aligned_writes(self):
        ota_c = (SIM_SRC / "sim_board_ota.c").read_text(encoding="utf-8")

        self.assertIn("SIM_BOARD_APP2_START_ADDR", ota_c)
        self.assertIn("SIM_BOARD_APP2_END_ADDR", ota_c)
        self.assertRegex(ota_c, r"addr\s*<\s*SIM_BOARD_APP2_START_ADDR")
        self.assertRegex(ota_c, r"end_addr\s*>\s*SIM_BOARD_APP2_END_ADDR")
        self.assertIn("0xFFU", ota_c)
        self.assertRegex(ota_c, r"\((data_len|write_len)\s*\+\s*3U\)\s*&\s*~3U")
        self.assertIn("flash_write(addr", ota_c)
        self.assertIn("flash_read(addr", ota_c)

    def test_ota_begin_does_not_block_on_full_app2_erase(self):
        ota_c = (SIM_SRC / "sim_board_ota.c").read_text(encoding="utf-8")
        begin_match = re.search(r"static uint8_t handle_begin[\s\S]+?static uint8_t handle_chunk", ota_c)
        self.assertIsNotNone(begin_match)

        self.assertNotIn("erase_app2", ota_c)
        self.assertNotIn("flash_erase_sector", begin_match.group(0))
        self.assertIn("sector_erased", ota_c)
        self.assertIn("ensure_sector_erased", ota_c)

    def test_ota_transfer_commands_use_compact_ack_responses(self):
        ota_c = (SIM_SRC / "sim_board_ota.c").read_text(encoding="utf-8")

        self.assertIn("append_transfer_ack", ota_c)
        for start, end in (
            ("static uint8_t handle_begin", "static uint8_t handle_chunk"),
            ("static uint8_t handle_chunk", "static uint8_t handle_end"),
            ("static uint8_t handle_end", "void sim_board_ota_init"),
        ):
            match = re.search(rf"{re.escape(start)}[\s\S]+?{re.escape(end)}", ota_c)
            self.assertIsNotNone(match)
            self.assertIn("append_transfer_ack", match.group(0))
            self.assertNotIn("append_status(*response_data)", match.group(0))

    def test_main_integrates_sim_business_and_ota_commands(self):
        main_c = (SIM_SRC / "main.c").read_text(encoding="utf-8")

        self.assertIn('#include "sim_board_business.h"', main_c)
        self.assertIn('#include "sim_board_ota.h"', main_c)
        self.assertIn("sim_board_business_append_telemetry(root);", main_c)
        self.assertIn("sim_board_business_publish_periodic_event", main_c)
        self.assertIn("sim_board_ota_handle_command", main_c)

        for command in ("ota_begin", "ota_chunk", "ota_end", "ota_abort", "ota_status", "sim_set", "sim_event"):
            self.assertIn(f'"{command}"', main_c)

        self.assertRegex(
            main_c,
            r"mqtt_subscribe\s*\(\s*\(char \*\)\s*system_config->mqtt_server\.topics\s*\[\s*MQTT_TOPIC_CMD_DOWN_INDEX\s*\]",
        )
        self.assertRegex(
            main_c,
            r"mqtt_subscribe\s*\(\s*\(char \*\)\s*system_config->mqtt_server\.topics\s*\[\s*MQTT_TOPIC_OTA_INDEX\s*\]",
        )

    def test_legacy_a4_upgrade_frames_are_routed_to_ota_module(self):
        business_c = (SIM_SRC / "sim_board_business.c").read_text(encoding="utf-8")
        ota_h = (SIM_SRC / "sim_board_ota.h").read_text(encoding="utf-8")

        self.assertIn('#include "sim_board_ota.h"', business_c)
        self.assertIn("sim_board_ota_handle_legacy_frame", ota_h)
        self.assertRegex(
            business_c,
            r"case\s+SIM_BOARD_FRM_TYPE_MAINBOARD_UPGRADE\s*:\s*return\s+sim_board_ota_handle_legacy_frame",
        )
        self.assertNotRegex(
            business_c,
            r"case\s+SIM_BOARD_FRM_TYPE_MAINBOARD_UPGRADE\s*:\s*return\s+create_legacy_received_data",
        )

    def test_v2_a5_device_type1_upgrade_subframes_route_to_ota_module(self):
        business_c = (SIM_SRC / "sim_board_business.c").read_text(encoding="utf-8")

        self.assertIn("parse_v2_legacy_subframe", business_c)
        self.assertIn("is_legacy_ota_command", business_c)
        self.assertIn("SIM_BOARD_CMD_A5_DEVICE_TYPE_1", business_c)
        self.assertRegex(
            business_c,
            r"frame->cmd_type\s*==\s*SIM_BOARD_CMD_A5_DEVICE_TYPE_1[\s\S]+?sim_board_ota_handle_legacy_frame",
        )
        self.assertIn("0xB0U", business_c)
        self.assertIn("0xB6U", business_c)

        # V2 workbook example:
        # FE A5 01 0008 FE B0 0000 3A1F2702 97651EA6
        self.assertIn("subframe->cmd_type = raw[1]", business_c)
        self.assertRegex(business_c, r"sim_board_crc32\s*\(\s*&raw\[1\]")

    def test_v2_legacy_ota_accepts_actual_512_byte_chunks(self):
        business_c = (SIM_SRC / "sim_board_business.c").read_text(encoding="utf-8")
        ota_h = (SIM_SRC / "sim_board_ota.h").read_text(encoding="utf-8")
        raw_max_match = re.search(r"#define\s+SIM_BOARD_RAW_FRAME_MAX\s+(\d+)U", business_c)
        max_chunks = int(re.search(r"#define\s+SIM_BOARD_OTA_MAX_CHUNKS\s+(\d+)U", ota_h).group(1), 10)

        self.assertIsNotNone(raw_max_match)
        raw_max = int(raw_max_match.group(1), 10)
        # FE A5 len + nested FE B2 len + 2-byte index + 512-byte data + two CRC32 fields.
        self.assertGreaterEqual(raw_max, 530)
        # Current app image is about 87 KB, so 512-byte field packets need about 171 chunks.
        self.assertGreaterEqual(max_chunks, 171)

    def test_legacy_b0_to_b6_ota_commands_are_implemented(self):
        ota_c = (SIM_SRC / "sim_board_ota.c").read_text(encoding="utf-8")

        for command in ("0xB0U", "0xB1U", "0xB2U", "0xB3U", "0xB4U", "0xB5U", "0xB6U"):
            self.assertIn(command, ota_c)

        for handler in (
            "handle_legacy_b0_start",
            "handle_legacy_b1_file_msg",
            "handle_legacy_b2_file_chunk",
            "handle_legacy_b3_check_lost",
            "handle_legacy_b4_end",
            "handle_legacy_b5_result",
            "handle_legacy_b6_break",
            "sim_board_ota_handle_legacy_frame",
        ):
            self.assertIn(handler, ota_c)

        self.assertIn("SIM_BOARD_UPGRADE_STATE_SUCCESS", ota_c)
        self.assertIn("write_upgrade_handle", ota_c)
        self.assertIn("crc32_flash(SIM_BOARD_APP2_START_ADDR", ota_c)

    def test_legacy_ota_pack_flags_use_original_bitset_layout(self):
        ota_c = (SIM_SRC / "sim_board_ota.c").read_text(encoding="utf-8")

        self.assertIn("ota_pack_flag_set", ota_c)
        self.assertIn("ota_pack_flag_is_set", ota_c)
        self.assertRegex(ota_c, r"pack_flag\s*\[\s*index\s*/\s*8U\s*\]")
        self.assertRegex(ota_c, r"1U\s*<<\s*\(\s*index\s*%\s*8U\s*\)")
        self.assertNotIn("ota_state.pack_flag[index] = 1U", ota_c)

    def test_sim_telemetry_contains_meter_environment_rs485_and_events(self):
        business_c = (SIM_SRC / "sim_board_business.c").read_text(encoding="utf-8")

        for json_key in (
            '"meter_hlw8112"',
            '"environment"',
            '"rs485"',
            '"lighting"',
            '"factory_test"',
            '"legacy_protocol"',
            '"emqx-gateway.simboard.telemetry.v1"',
            '"emqx-gateway.simboard.event.v1"',
        ):
            self.assertIn(json_key, business_c)

        for event_name in (
            "voltage_high",
            "current_high",
            "temperature_high",
            "rs485_offline",
            "rs485_recovered",
        ):
            self.assertIn(event_name, business_c)

    def test_sim_board_firmware_version_can_be_overridden_for_ota_switch_tests(self):
        build_py = ROOT / "开发环境" / "scripts" / "build_local_emqx_firmware.py"
        build_ps1 = ROOT / "开发环境" / "scripts" / "Build-SimBoard-Firmware.ps1"
        business_c = (SIM_SRC / "sim_board_business.c").read_text(encoding="utf-8")

        self.assertIn('parser.add_argument("--define"', build_py.read_text(encoding="utf-8"))
        self.assertIn("[string]$FirmwareVersion", build_ps1.read_text(encoding="utf-8"))
        self.assertIn("SIM_BOARD_FW_VERSION", business_c)
        self.assertIn('"firmware_version"', business_c)


if __name__ == "__main__":
    unittest.main()
