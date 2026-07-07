import re
import tempfile
import unittest
import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "mqttv5(1)" / "src"
DRIVER = ROOT / "mqttv5(1)" / "driver"


class LocalEmqxFirmwareConfigTests(unittest.TestCase):
    def test_w5500_mqtt_target_points_to_local_emqx_plain_listener(self):
        task_w5500 = (SOURCE / "task_w5500.c").read_text(encoding="utf-8")

        self.assertRegex(task_w5500, r"\.port\s*=\s*1883\s*,")
        self.assertRegex(task_w5500, r"\.ip\s*=\s*\{\s*192\s*,\s*168\s*,\s*0\s*,\s*110\s*\}")
        self.assertRegex(task_w5500, r"w5500_socket_mqtt\s*=\s*\{[^}]*\.sn\s*=\s*0", re.S)
        self.assertRegex(task_w5500, r"w5500_socket_modbus\s*=\s*\{[^}]*\.sn\s*=\s*1", re.S)

    def test_firmware_has_switchable_plain_mqtt_transport_for_board_debug(self):
        main_c = (SOURCE / "main.c").read_text(encoding="utf-8")

        self.assertRegex(main_c, r"#define\s+MQTT_USE_TLS\s+0")
        self.assertRegex(main_c, r"send\s*\(\s*w5500_socket_mqtt\.sn\s*,\s*data\s*,\s*len\s*\)")
        self.assertRegex(main_c, r"recv\s*\(\s*w5500_socket_mqtt\.sn\s*,\s*data\s*,\s*len\s*\)")

    def test_firmware_config_has_emqx_server_and_five_topic_contract(self):
        config_h = (SOURCE / "config.h").read_text(encoding="utf-8")
        config_c = (SOURCE / "config.c").read_text(encoding="utf-8")

        self.assertRegex(config_h, r"char\s+host\s*\[\s*64\s*\]")
        self.assertRegex(config_h, r"uint16_t\s+port\s*;")
        self.assertRegex(config_h, r"char\s+username\s*\[\s*32\s*\]")
        self.assertRegex(config_h, r"char\s+password\s*\[\s*32\s*\]")
        self.assertRegex(config_h, r"char\s+topics\s*\[\s*5\s*\]\s*\[\s*96\s*\]")
        self.assertRegex(config_h, r"uint8_t\s+qos\s*\[\s*5\s*\]")
        self.assertRegex(config_h, r"char\s+ntp_server\s*\[\s*64\s*\]")
        self.assertNotIn("domain[32]", config_h)

        self.assertIn('.host = "192.168.0.110"', config_c)
        self.assertRegex(config_c, r"\.port\s*=\s*1883")
        for topic in self._expected_topics():
            self.assertIn(topic, config_c)
        for forbidden in ("www.baidu.com", "aliyun", "ProductKey", "DeviceSecret", "securemode"):
            self.assertNotIn(forbidden, config_c)

    def test_firmware_uses_configured_five_topics_for_mqtt5_business_logic(self):
        main_c = (SOURCE / "main.c").read_text(encoding="utf-8")

        self.assertIn("MQTT_TOPIC_TELEMETRY_UP_INDEX", main_c)
        self.assertRegex(main_c, r"system_config->mqtt_server\.topics\s*\[\s*MQTT_TOPIC_CMD_DOWN_INDEX\s*\]")
        self.assertIn("mqtt_publish_json(MQTT_TOPIC_TELEMETRY_UP_INDEX", main_c)
        self.assertIn("mqtt_publish_json(MQTT_TOPIC_EVENT_UP_INDEX", main_c)
        self.assertIn("mqtt_publish_json(MQTT_TOPIC_STATUS_UP_INDEX", main_c)
        self.assertIn("mqtt_publish_json(MQTT_TOPIC_DEBUG_UP_INDEX", main_c)
        self.assertNotRegex(main_c, r"static\s+const\s+char\s+MQTT_TOPIC_[A-Z_]+\[\]")

    def test_firmware_standard_json_business_contract(self):
        main_c = (SOURCE / "main.c").read_text(encoding="utf-8")
        build_script_params = ROOT / "mqttv5(1)" / "eide" / "build" / "Debug" / "builder.params"
        params_text = build_script_params.read_text(encoding="utf-8")

        self.assertIn("../cjson", params_text)
        self.assertIn("../cjson/cJSON.c", params_text)
        self.assertIn('#include "cJSON.h"', main_c)
        self.assertIn("mqtt_handle_command_json", main_c)
        for command in ("ping", "get_status", "get_config", "set_config", "reboot"):
            self.assertIn(f'"{command}"', main_c)
        self.assertIn('"emqx-gateway.response.v1"', main_c)
        self.assertIn('"emqx-gateway.telemetry.v1"', main_c)
        self.assertIn('"emqx-gateway.status.v1"', main_c)
        self.assertIn("MQTT_TOPIC_EVENT_UP_INDEX", main_c)
        self.assertIn("MQTT_TOPIC_STATUS_UP_INDEX", main_c)
        self.assertIn("MQTT_TOPIC_DEBUG_UP_INDEX", main_c)

    def test_firmware_telemetry_period_is_short_enough_for_local_integration(self):
        main_c = (SOURCE / "main.c").read_text(encoding="utf-8")

        match = re.search(r"#define\s+MQTT_TELEMETRY_TICKS\s+(\d+)U?", main_c)
        self.assertIsNotNone(match)
        self.assertLessEqual(int(match.group(1)), 400)
        self.assertNotIn("0x3FFF", main_c)

    def test_firmware_uses_cmd_down_as_only_mqtt_subscription(self):
        main_c = (SOURCE / "main.c").read_text(encoding="utf-8")

        subscribe_calls = re.findall(r"mqtt_subscribe\s*\(\s*\(char \*\)\s*system_config->mqtt_server\.topics\s*\[[^;]+;", main_c)
        self.assertEqual(len(subscribe_calls), 1)
        self.assertIn("MQTT_TOPIC_CMD_DOWN_INDEX", subscribe_calls[0])
        self.assertNotIn("MQTT_TOPIC_TELEMETRY_UP_INDEX", subscribe_calls[0])

    def test_firmware_config_has_version_and_crc_guard(self):
        config_h = (SOURCE / "config.h").read_text(encoding="utf-8")
        config_c = (SOURCE / "config.c").read_text(encoding="utf-8")

        self.assertIn("SYSTEM_CONFIG_MAGIC", config_h)
        self.assertIn("SYSTEM_CONFIG_VERSION", config_h)
        self.assertRegex(config_h, r"uint32_t\s+magic\s*;")
        self.assertRegex(config_h, r"uint32_t\s+version\s*;")
        self.assertRegex(config_h, r"uint32_t\s+crc\s*;")
        self.assertIn("config_calculate_crc", config_c)
        self.assertIn("config_is_valid", config_c)
        self.assertRegex(config_c, r"config->magic\s*!=\s*SYSTEM_CONFIG_MAGIC")
        self.assertRegex(config_c, r"config->version\s*!=\s*SYSTEM_CONFIG_VERSION")

    def test_modbus_management_channel_exposes_full_config_block(self):
        main_c = (SOURCE / "main.c").read_text(encoding="utf-8")
        management_cs = ROOT / "mqttv5_tool" / "mqttv5_tool" / "Management.cs"
        text = management_cs.read_text(encoding="utf-8")

        self.assertRegex(main_c, r"memcpy\s*\(\s*nmbs_server\.regs\s*,\s*&system_config_temp\s*,\s*sizeof\s*\(\s*system_config_t\s*\)\s*\)")
        self.assertIn("ConfigSizeBytes = 768", text)
        self.assertIn("TopicCount = 5", text)
        self.assertIn("TopicLength = 96", text)
        self.assertIn("NtpServerLength = 64", text)

    def test_app_linker_script_starts_at_bootloader_safe_offset(self):
        linker_script = ROOT / "mqttv5(1)" / "ldscripts" / "hc32f460_app_0x4000.lds"
        text = linker_script.read_text(encoding="utf-8")

        self.assertRegex(text, r"FLASH\s+\(rx\):\s+ORIGIN\s+=\s+0x00004000")
        self.assertRegex(text, r"LENGTH\s+=\s+496K")

    def test_app_build_relocates_interrupt_vector_table_to_app_base(self):
        build_script = ROOT / "开发环境" / "scripts" / "build_local_emqx_firmware.py"
        text = build_script.read_text(encoding="utf-8")

        self.assertIn("VECT_TAB_OFFSET=0x4000", text)

    def test_hex_range_parser_handles_extended_segment_address_records(self):
        build_script = ROOT / "开发环境" / "scripts" / "build_local_emqx_firmware.py"
        spec = importlib.util.spec_from_file_location("build_local_emqx_firmware", build_script)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        with tempfile.NamedTemporaryFile("w", suffix=".hex", delete=False, encoding="ascii") as hex_file:
            hex_file.write(":020000021000EC\n")
            hex_file.write(":1000000000000000000000000000000000000000F0\n")
            hex_file.write(":00000001FF\n")
            hex_path = Path(hex_file.name)

        try:
            minimum, maximum = module.hex_address_range(hex_path)
        finally:
            hex_path.unlink(missing_ok=True)

        self.assertEqual(minimum, 0x10000)
        self.assertEqual(maximum, 0x1000F)

    def test_w5500_pins_match_current_hc32_board_wiring(self):
        spi_c = (DRIVER / "spi.c").read_text(encoding="utf-8")
        wiz_platform_c = (SOURCE / "wiz_platform.c").read_text(encoding="utf-8")

        self.assertRegex(spi_c, r"#define\s+SPI_UNIT\s+CM_SPI3")
        self.assertRegex(spi_c, r"#define\s+SPI_SS_PORT\s+GPIO_PORT_A")
        self.assertRegex(spi_c, r"#define\s+SPI_SS_PIN\s+GPIO_PIN_01")
        self.assertRegex(spi_c, r"#define\s+SPI_SCK_PORT\s+GPIO_PORT_B")
        self.assertRegex(spi_c, r"#define\s+SPI_SCK_PIN\s+GPIO_PIN_13")
        self.assertRegex(spi_c, r"#define\s+SPI_MISO_PORT\s+GPIO_PORT_B")
        self.assertRegex(spi_c, r"#define\s+SPI_MISO_PIN\s+GPIO_PIN_14")
        self.assertRegex(spi_c, r"#define\s+SPI_MOSI_PORT\s+GPIO_PORT_B")
        self.assertRegex(spi_c, r"#define\s+SPI_MOSI_PIN\s+GPIO_PIN_15")
        self.assertRegex(wiz_platform_c, r"GPIO_Init\s*\(\s*GPIO_PORT_A\s*,\s*GPIO_PIN_10")
        self.assertRegex(wiz_platform_c, r"GPIO_SetPins\s*\(\s*GPIO_PORT_A\s*,\s*GPIO_PIN_10")
        self.assertRegex(wiz_platform_c, r"GPIO_ResetPins\s*\(\s*GPIO_PORT_A\s*,\s*GPIO_PIN_10")

    def _expected_topics(self):
        return {
            "/local_pk/hc32f460_dev/user/telemetry/up",
            "/local_pk/hc32f460_dev/user/event/up",
            "/local_pk/hc32f460_dev/user/status/up",
            "/local_pk/hc32f460_dev/user/cmd/down",
            "/local_pk/hc32f460_dev/user/debug/up",
        }


if __name__ == "__main__":
    unittest.main()
