import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REAL_PROJECT = ROOT / "mqttv5_real_board"
REAL_SRC = REAL_PROJECT / "src"
SIM_PROJECT = ROOT / "mqttv5_sim_board"
SIM_SRC = SIM_PROJECT / "src"
DOCUMENTED_REQUEST_TOPIC = "v1/devices/request/{device_name}"
DOCUMENTED_RESPONSE_TOPIC = "v1/devices/response/{device_name}"


class RealBoardBusinessContractTests(unittest.TestCase):
    def test_real_board_project_is_separate_from_verified_virtual_project(self):
        self.assertTrue(REAL_PROJECT.exists(), "mqttv5_real_board project folder must exist")
        self.assertNotEqual(REAL_PROJECT, ROOT / "mqttv5(1)")
        self.assertTrue((REAL_PROJECT / "ldscripts" / "hc32f460_app_0x4000.lds").exists())

    def test_real_business_layer_restores_legacy_business_domains(self):
        business_h = (REAL_SRC / "real_board_business.h").read_text(encoding="utf-8")
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")

        for symbol in (
            "real_board_business_init",
            "real_board_business_tick",
            "real_board_business_update_meter_hlw8112",
            "real_board_business_update_environment",
            "real_board_business_update_rs485",
            "real_board_business_append_telemetry",
            "real_board_business_append_status",
        ):
            self.assertIn(symbol, business_h)
            self.assertIn(symbol, business_c)

        for json_key in (
            '"meter_hlw8112"',
            '"environment"',
            '"rs485"',
            '"lighting"',
            '"device"',
            '"peripherals"',
            '"factory_test"',
        ):
            self.assertIn(json_key, business_c)

        self.assertIn('"emqx-gateway.realboard.telemetry.v1"', business_c)
        self.assertIn('"emqx-gateway.realboard.event.v1"', business_c)
        self.assertNotIn('"hello from board"', business_c)

    def test_legacy_downlink_parser_is_enabled_by_default_and_gated(self):
        business_h = (REAL_SRC / "real_board_business.h").read_text(encoding="utf-8")
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")

        self.assertIn("#define REAL_BOARD_ENABLE_LEGACY_FRAME 1", business_h)
        self.assertIn("#if REAL_BOARD_ENABLE_LEGACY_FRAME", business_c)
        self.assertIn("legacy_downlink_supported", business_c)
        self.assertNotIn('cJSON_AddBoolToObject(object, "legacy_downlink_supported", 0)', business_c)
        self.assertRegex(
            main_c,
            r"#if\s+REAL_BOARD_ENABLE_LEGACY_FRAME[\s\S]+real_board_business_handle_legacy_command[\s\S]+#endif",
        )
        self.assertRegex(
            main_c,
            r"if\s*\(!cJSON_IsString\(cmd_item\)[\s\S]+mqtt_publish_response\(id,\s*NULL,\s*0,\s*400,\s*\"cmd must be a string\"",
        )

    def test_legacy_compatibility_code_is_retained_but_not_the_default_path(self):
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")

        self.assertIn("create_legacy_received_data", business_c)
        self.assertIn('"received"', business_c)
        self.assertIn('"legacy frame received"', business_c)
        self.assertIn('"outside_device_passthrough"', business_c)
        self.assertNotIn('"unsupported legacy A1 command"', business_c)
        self.assertNotIn('"unsupported legacy A2 command"', business_c)
        self.assertNotIn('"legacy frame type is reserved in this build"', business_c)

    def test_real_board_accepts_local_mqtt_msg_payload_wrapper(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")

        self.assertIn("mqtt_unwrap_local_payload_json", main_c)
        self.assertIn("mqtt_try_parse_payload_json_text", main_c)
        self.assertIn("mqtt_try_parse_payload_base64_json", main_c)
        self.assertRegex(
            main_c,
            r"cJSON_GetObjectItemCaseSensitive\s*\(\s*root\s*,\s*\"payload\"\s*\)",
        )
        self.assertRegex(
            main_c,
            r"mqtt_try_parse_payload_json_text\s*\(\s*payload_item->valuestring\s*\)",
        )
        self.assertRegex(
            main_c,
            r"mqtt_try_parse_payload_base64_json\s*\(\s*payload_item->valuestring\s*\)",
        )
        self.assertRegex(
            main_c,
            r"cJSON_ParseWithLength\s*\(\s*payload_text\s*,\s*strlen\s*\(\s*payload_text\s*\)\s*\)",
        )
        self.assertRegex(
            main_c,
            r"cJSON_ParseWithLength\s*\(\s*mqtt_payload_json_decode_buf\s*,\s*decoded_len\s*\)",
        )
        self.assertIn("command_root", main_c)
        self.assertRegex(
            main_c,
            r"mqtt_get_json_id\s*\(\s*command_root[\s\S]+mqtt_get_json_id\s*\(\s*root",
        )

    def test_real_board_payload_wrapper_can_carry_legacy_base64_or_hex_business_frame(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")

        self.assertRegex(
            business_c,
            r"cJSON_GetObjectItemCaseSensitive\s*\(\s*root\s*,\s*\"payload\"\s*\)",
        )
        self.assertIn("mbedtls_base64_decode", main_c)
        self.assertIn("mbedtls_base64_decode", business_c)
        self.assertIn("decode_legacy_hex_frame", business_c)
        self.assertIn("legacy_text_looks_hex", business_c)
        self.assertRegex(
            business_c,
            r"decode_legacy_frame[\s\S]+legacy_text_looks_hex[\s\S]+decode_legacy_hex_frame[\s\S]+mbedtls_base64_decode",
        )
        self.assertRegex(
            business_c,
            r"switch\s*\(\s*frame\.frm_type\s*\)[\s\S]+REAL_BOARD_FRM_TYPE_MAINBOARD_PARA_SET[\s\S]+handle_a1_command",
        )
        self.assertRegex(
            business_c,
            r"switch\s*\(\s*frame\.frm_type\s*\)[\s\S]+REAL_BOARD_FRM_TYPE_MAINBOARD_CONTROL[\s\S]+handle_a2_command",
        )
        self.assertRegex(
            business_c,
            r"switch\s*\(\s*frame\.frm_type\s*\)[\s\S]+REAL_BOARD_FRM_TYPE_OUTSIDE_DEVICE[\s\S]+handle_a5_command",
        )
        self.assertIn('"outside_device_passthrough"', business_c)
        self.assertIn("queue_a5_request", business_c)

    def test_real_telemetry_is_used_by_mqtt_task(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")

        self.assertIn('#include "real_board_business.h"', main_c)
        self.assertIn("real_board_business_init();", main_c)
        self.assertIn("real_board_business_tick();", main_c)
        self.assertIn("real_board_business_append_telemetry(root);", main_c)
        self.assertIn("real_board_business_append_status(data);", main_c)
        self.assertIn("static void mqtt_publish_real_event(void)", main_c)
        self.assertIn("real_board_business_publish_event(root)", main_c)
        self.assertIn("mqtt_publish_real_event();", main_c)
        self.assertNotIn('"message", "hello from board"', main_c)

    def test_real_telemetry_mqtt_buffers_are_large_enough_for_full_business_payload(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")
        packet = re.search(r"#define\s+MQTT_PACKET_BUF_SIZE\s+(\d+)", main_c)
        json_buf = re.search(r"#define\s+MQTT_JSON_BUF_SIZE\s+(\d+)", main_c)

        self.assertIsNotNone(packet)
        self.assertIsNotNone(json_buf)
        self.assertGreaterEqual(int(json_buf.group(1)), 4096)
        self.assertGreaterEqual(int(packet.group(1)), int(json_buf.group(1)) + 512)

    def test_real_project_builder_can_target_new_project_folder(self):
        build_py = ROOT / "开发环境" / "scripts" / "build_local_emqx_firmware.py"
        build_ps1 = ROOT / "开发环境" / "scripts" / "Build-RealBoard-Firmware.ps1"
        params = REAL_PROJECT / "eide" / "build" / "Debug" / "builder.params"

        build_text = build_py.read_text(encoding="utf-8")
        ps1_text = build_ps1.read_text(encoding="utf-8")
        params_text = params.read_text(encoding="utf-8")

        self.assertIn("--project-root", build_text)
        self.assertIn("--output-name", build_text)
        self.assertIn("mqttv5_real_board", ps1_text)
        self.assertIn("../src/real_board_business.c", params_text)
        self.assertIn("../src/real_board_ota.c", params_text)
        self.assertIn("../src/real_board_tls_certs.c", params_text)

    def test_public_emqx_defaults_are_build_time_configurable_for_real_and_sim_firmware(self):
        real_config_c = (REAL_SRC / "config.c").read_text(encoding="utf-8")
        sim_config_c = (SIM_SRC / "config.c").read_text(encoding="utf-8")
        real_build = (ROOT / "开发环境" / "scripts" / "Build-RealBoard-Firmware.ps1").read_text(encoding="utf-8")
        sim_build = (ROOT / "开发环境" / "scripts" / "Build-SimBoard-Firmware.ps1").read_text(encoding="utf-8")

        for token in (
            "REAL_BOARD_DEFAULT_MQTT_HOST",
            "REAL_BOARD_DEFAULT_MQTT_PORT",
            "REAL_BOARD_DEFAULT_MQTT_USERNAME",
            "REAL_BOARD_DEFAULT_MQTT_PASSWORD",
        ):
            self.assertIn(token, real_config_c)
        self.assertIn(".mqtt_server.username = REAL_BOARD_DEFAULT_MQTT_USERNAME", real_config_c)
        self.assertIn(".mqtt_server.password = REAL_BOARD_DEFAULT_MQTT_PASSWORD", real_config_c)
        self.assertIn("[string[]]$AdditionalDefine", real_build)

        for token in (
            "SIM_BOARD_DEFAULT_MQTT_HOST",
            "SIM_BOARD_DEFAULT_MQTT_PORT",
            "SIM_BOARD_DEFAULT_MQTT_USERNAME",
            "SIM_BOARD_DEFAULT_MQTT_PASSWORD",
        ):
            self.assertIn(token, sim_config_c)
        self.assertIn(".mqtt_server.username = SIM_BOARD_DEFAULT_MQTT_USERNAME", sim_config_c)
        self.assertIn(".mqtt_server.password = SIM_BOARD_DEFAULT_MQTT_PASSWORD", sim_config_c)
        self.assertIn("[string[]]$AdditionalDefine", sim_build)

    def test_real_project_integrates_ota_and_real_only_payloads(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")
        ota_h = (REAL_SRC / "real_board_ota.h").read_text(encoding="utf-8")
        ota_c = (REAL_SRC / "real_board_ota.c").read_text(encoding="utf-8")
        combined_ota = ota_h + "\n" + ota_c

        self.assertIn('#include "real_board_ota.h"', main_c)
        self.assertIn("real_board_ota_init();", main_c)
        self.assertIn("real_board_ota_append_status(data);", main_c)
        self.assertIn("real_board_ota_handle_command", main_c)
        self.assertIn("REAL_BOARD_OTA_REBOOT_AFTER_END", ota_h)
        self.assertIn("*reboot_requested = REAL_BOARD_OTA_REBOOT_AFTER_END ? 1U : 0U", ota_c)
        self.assertRegex(
            main_c,
            r"mqtt_subscribe\s*\(\s*\(char \*\)\s*mqtt_get_request_topic\s*\(\s*\)",
        )

        for command in ("ota_begin", "ota_chunk", "ota_end", "ota_abort", "ota_status", "real_set", "real_event"):
            self.assertIn(f'"{command}"', main_c)

        for required in (
            "#define REAL_BOARD_APP1_START_ADDR 0x00004000UL",
            "#define REAL_BOARD_APP2_START_ADDR 0x00042000UL",
            "#define REAL_BOARD_APP2_END_ADDR 0x00076000UL",
            "#define REAL_BOARD_UPGRADE_STATE_ADDR 0x0007A000UL",
            "#define REAL_BOARD_UPGRADE_STATE_SUCCESS 0xA55AU",
        ):
            self.assertIn(required, combined_ota)

        for forbidden in (
            "triangle_wave",
            "sim_board",
            "SIM_BOARD",
            "emqx-gateway.simboard",
            "simulated",
        ):
            self.assertNotIn(forbidden, business_c + "\n" + main_c + "\n" + combined_ota)

    def test_ota_chunk_window_covers_entire_app2_partition(self):
        ota_h = (REAL_SRC / "real_board_ota.h").read_text(encoding="utf-8")

        app2_start = int(re.search(r"#define\s+REAL_BOARD_APP2_START_ADDR\s+0x([0-9A-Fa-f]+)UL", ota_h).group(1), 16)
        app2_end = int(re.search(r"#define\s+REAL_BOARD_APP2_END_ADDR\s+0x([0-9A-Fa-f]+)UL", ota_h).group(1), 16)
        max_chunks = int(re.search(r"#define\s+REAL_BOARD_OTA_MAX_CHUNKS\s+(\d+)U", ota_h).group(1), 10)
        max_chunk_size = int(re.search(r"#define\s+REAL_BOARD_OTA_MAX_CHUNK_SIZE\s+(\d+)U", ota_h).group(1), 10)
        pack_flag_bytes = int(re.search(r"#define\s+REAL_BOARD_OTA_PACK_FLAG_BYTES\s+(\d+)U", ota_h).group(1), 10)

        self.assertGreaterEqual(max_chunks * max_chunk_size, app2_end - app2_start)
        self.assertEqual(128, pack_flag_bytes)
        self.assertEqual(pack_flag_bytes * 8, max_chunks)
        self.assertIn("uint8_t pack_flag[REAL_BOARD_OTA_PACK_FLAG_BYTES]", ota_h)

    def test_real_project_keeps_bootloader_safe_app_link_address(self):
        linker_script = REAL_PROJECT / "ldscripts" / "hc32f460_app_0x4000.lds"
        text = linker_script.read_text(encoding="utf-8")

        self.assertRegex(text, r"FLASH\s+\(rx\):\s+ORIGIN\s+=\s+0x00004000")
        self.assertRegex(text, r"LENGTH\s+=\s+248K")

    def test_safe_flash_script_uses_page_staging_to_keep_flm_stack_clear(self):
        flash_script = ROOT / "开发环境" / "scripts" / "Flash-MQTTv5-STLink.ps1"
        text = flash_script.read_text(encoding="utf-8")

        self.assertIn("$pageStageDir", text)
        self.assertIn("$programPageFiles", text)
        self.assertIn("$bufferBase = [uint32]0x20004000", text)
        self.assertIn("$stackReserveBytes = [uint32]0x00004000", text)
        self.assertIn("$bufferEnd", text)
        self.assertIn("APP page staging buffer would overlap FLM stack reserve", text)
        self.assertIn("load_image {$(ConvertTo-TclPath $pagePath)} $(Format-Hex32 $bufferBase) bin", text)
        self.assertIn("$(Format-Hex32 $bufferBase) 0x00000000", text)
        self.assertNotIn("load_image {$(ConvertTo-TclPath $appBin)} $(Format-Hex32 $bufferBase) bin", text)

    def test_safe_flash_script_resets_faulted_target_before_running_flm(self):
        flash_script = ROOT / "开发环境" / "scripts" / "Flash-MQTTv5-STLink.ps1"
        text = flash_script.read_text(encoding="utf-8")

        reset_index = text.find('$lines.Add("reset halt")')
        flm_index = text.find('$lines.Add("load_image {$(ConvertTo-TclPath $FlmBlob)}')

        self.assertGreaterEqual(reset_index, 0, "safe flash script must reset a faulted MCU before FLM execution")
        self.assertGreater(flm_index, reset_index, "reset halt must run before loading/executing the FLM blob")

    def test_real_project_matches_documented_flash_parameter_partition(self):
        config_c = (REAL_SRC / "config.c").read_text(encoding="utf-8")

        self.assertIn("#define FLASH_WRITE_END_ADDR 0x00080000UL", config_c)
        self.assertIn("#define FLASH_PAGE_SIZE 0x00002000UL", config_c)
        self.assertIn("#define SYSTEM_CONFIG_FLASH_ADDR (FLASH_WRITE_END_ADDR - (5UL * FLASH_PAGE_SIZE))", config_c)
        self.assertIn("#define SYSTEM_CONFIG_LEGACY_FLASH_ADDR (FLASH_WRITE_END_ADDR - (2UL * FLASH_PAGE_SIZE))", config_c)
        self.assertIn("config_is_valid((system_config_t *)SYSTEM_CONFIG_LEGACY_FLASH_ADDR)", config_c)
        self.assertIn("config_save(&system_config_temp);", config_c)

    def test_real_project_rejects_invalid_static_network_config_before_flash_write(self):
        config_h = (REAL_SRC / "config.h").read_text(encoding="utf-8")
        config_c = (REAL_SRC / "config.c").read_text(encoding="utf-8")
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")

        self.assertIn("uint8_t config_save(system_config_t *system_config_in);", config_h)
        self.assertIn("config_payload_is_valid", config_c)
        self.assertIn("config_ipv4_is_usable_host", config_c)
        self.assertIn("config_subnet_mask_is_valid", config_c)
        self.assertIn("config_same_subnet", config_c)
        self.assertRegex(config_c, r"config->eth\.mode\s*>\s*1U")
        self.assertRegex(
            config_c,
            r"if\s*\(\s*!config_payload_is_valid\s*\(\s*system_config_in\s*\)\s*\)[\s\S]+?return\s+0\s*;[\s\S]+?flash_erase_sector",
        )
        self.assertIn("if (config_save(&system_config_temp))", main_c)

    def test_qos2_is_configurable_and_acknowledged(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")
        config_c = (REAL_SRC / "config.c").read_text(encoding="utf-8")

        self.assertIn(".mqtt_server.qos = {2, 2, 2, 2, 2}", config_c)
        self.assertIn("qos_item->valueint > 2", main_c)
        self.assertIn('"qos %d must be 0, 1 or 2"', main_c)
        self.assertIn("MQTTV5Serialize_pubrec", main_c)
        self.assertIn("MQTTV5Serialize_pubrel", main_c)
        self.assertIn("MQTTV5Serialize_pubcomp", main_c)
        self.assertIn("MQTTV5Deserialize_ack", main_c)
        self.assertNotIn('"unsupported_qos"', main_c)

    def test_mqtt_topics_match_request_response_server_contract(self):
        config_c = (REAL_SRC / "config.c").read_text(encoding="utf-8")
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")

        self.assertIn('REAL_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX "v1/devices/request/"', config_c)
        self.assertIn('REAL_BOARD_DEFAULT_RESPONSE_TOPIC_PREFIX "v1/devices/response/"', config_c)
        self.assertIn('REAL_BOARD_DEFAULT_DEVICE_TYPE_NAME "GM400"', config_c)
        self.assertIn('snprintf(request_topic, sizeof(request_topic), "%s%s"', config_c)
        self.assertIn('snprintf(response_topic, sizeof(response_topic), "%s%s"', config_c)
        self.assertIn("system_config_temp.mqtt_server.topics[0]", config_c)
        self.assertIn("system_config_temp.mqtt_server.topics[1]", config_c)
        self.assertIn("request_topic", config_c)
        self.assertIn("response_topic", config_c)
        self.assertNotIn("city/{city_id}/pole/{pole_id}/device/{device_name}", config_c)
        self.assertNotIn("/local_pk/hc32f460_dev/user", config_c)

        self.assertIn("MQTT_TOPIC_TELEMETRY_UP_INDEX = 0", main_c)
        self.assertIn("MQTT_TOPIC_CMD_DOWN_INDEX = 1", main_c)
        self.assertIn("MQTT_TOPIC_EVENT_UP_INDEX = 2", main_c)
        self.assertIn("MQTT_TOPIC_OTA_INDEX = 3", main_c)
        self.assertIn("MQTT_TOPIC_DEBUG_UP_INDEX = 4", main_c)
        self.assertNotIn("MQTT_TOPIC_STATUS_UP_INDEX", main_c)
        self.assertIn('MQTT_SERVER_REQUEST_TOPIC_PREFIX "v1/devices/request/"', main_c)
        self.assertIn('MQTT_SERVER_RESPONSE_TOPIC_PREFIX "v1/devices/response/"', main_c)
        self.assertIn("mqtt_get_request_topic", main_c)
        self.assertIn("mqtt_get_response_topic", main_c)
        self.assertRegex(
            main_c,
            r"mqtt_get_request_topic[\s\S]+system_config->mqtt_server\.topics\s*\[\s*MQTT_TOPIC_CMD_DOWN_INDEX\s*\]",
        )
        self.assertRegex(
            main_c,
            r"mqtt_get_response_topic[\s\S]+system_config->mqtt_server\.topics\s*\[\s*MQTT_TOPIC_TELEMETRY_UP_INDEX\s*\]",
        )
        self.assertIn("mqtt_publish_response_json", main_c)
        self.assertRegex(
            main_c,
            r"mqtt_publish_legacy_payload_envelope_response[\s\S]+mqtt_publish_response_json",
        )
        self.assertRegex(
            main_c,
            r"mqtt_publish_status[\s\S]+mqtt_publish_response_json",
        )
        self.assertRegex(
            main_c,
            r"mqtt_subscribe\s*\(\s*\(char \*\)\s*mqtt_get_request_topic\s*\(\s*\)",
        )

    def test_real_board_uses_status_topic_for_will_not_reserved_sys_topics(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")

        self.assertNotIn("$SYS/brokers/", main_c)
        self.assertNotIn("mqtt_get_sys_connected_topic", main_c)
        self.assertNotIn("mqtt_get_sys_disconnected_topic", main_c)
        self.assertNotIn("mqtt_publish_client_lifecycle", main_c)
        self.assertIn("data.willFlag = 1;", main_c)
        self.assertIn("data.will.qos = 0;", main_c)
        self.assertIn("data.will.retained = 1;", main_c)
        self.assertIn("data.will.topicName.cstring = (char *)mqtt_get_status_topic();", main_c)
        self.assertIn('data.will.message.cstring = mqtt_make_status_payload("offline", "mqtt_lwt");', main_c)
        self.assertIn("MQTTProperties will_properties = MQTTProperties_initializer;", main_c)
        self.assertIn("data.will.properties = &will_properties;", main_c)
        self.assertIn('mqtt_publish_lifecycle_status("online", "mqtt_connected");', main_c)

    def test_bare_payload_wrapper_is_treated_as_legacy_payload_envelope(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")

        self.assertRegex(
            main_c,
            r"mqtt_is_legacy_payload_envelope[\s\S]+"
            r"cJSON_GetObjectItemCaseSensitive\s*\(\s*root\s*,\s*\"payload\"\s*\)[\s\S]+"
            r"return\s+1;",
        )
        self.assertRegex(
            main_c,
            r"legacy_payload_envelope\s*=\s*mqtt_is_legacy_payload_envelope[\s\S]+"
            r"if\s*\(\s*legacy_payload_envelope\s*\)[\s\S]+"
            r"mqtt_publish_legacy_payload_envelope_response",
        )
        for alias in (
            'cJSON_GetObjectItemCaseSensitive(root, "payload")',
            'cJSON_GetObjectItemCaseSensitive(item, "payload")',
            'cJSON_GetObjectItemCaseSensitive(item, "msg")',
            'cJSON_GetObjectItemCaseSensitive(item, "params")',
            'cJSON_GetObjectItemCaseSensitive(item, "frame")',
        ):
            self.assertIn(alias, business_c)
        self.assertIn('cJSON_GetObjectItemCaseSensitive(root, "data")', business_c)
        self.assertIn('cJSON_GetObjectItemCaseSensitive(root, "params")', business_c)
        self.assertIn('cJSON_AddStringToObject(root, "payload", payload_text);', main_c)
        self.assertIn('mqtt_add_envelope_item_if_present(root, request, "mutable");', main_c)
        self.assertIn('mqtt_add_envelope_item_if_present(root, request, "qos");', main_c)

    def test_sim_board_defaults_and_legacy_aliases_follow_request_response_contract(self):
        sim_config_c = (SIM_SRC / "config.c").read_text(encoding="utf-8")
        sim_real_business_c = (SIM_SRC / "real_board_business.c").read_text(encoding="utf-8")
        sim_business_c = (SIM_SRC / "sim_board_business.c").read_text(encoding="utf-8")

        self.assertIn('SIM_BOARD_DEFAULT_REQUEST_TOPIC_PREFIX "v1/devices/request/"', sim_config_c)
        self.assertIn('SIM_BOARD_DEFAULT_RESPONSE_TOPIC_PREFIX "v1/devices/response/"', sim_config_c)
        self.assertIn('SIM_BOARD_DEFAULT_DEVICE_TYPE_NAME "GM400"', sim_config_c)
        self.assertIn('snprintf(request_topic, sizeof(request_topic), "%s%s"', sim_config_c)
        self.assertIn('snprintf(response_topic, sizeof(response_topic), "%s%s"', sim_config_c)
        self.assertNotIn("city/{city_id}/pole/{pole_id}/device/{device_name}", sim_config_c)

        for source in (sim_real_business_c, sim_business_c):
            for alias in (
                'cJSON_GetObjectItemCaseSensitive(root, "payload")',
                'cJSON_GetObjectItemCaseSensitive(root, "msg")',
                'cJSON_GetObjectItemCaseSensitive(item, "payload")',
                'cJSON_GetObjectItemCaseSensitive(item, "msg")',
                'cJSON_GetObjectItemCaseSensitive(item, "params")',
                'cJSON_GetObjectItemCaseSensitive(item, "frame")',
            ):
                self.assertIn(alias, source)

    def test_real_board_ignores_its_own_publish_json_when_subscribed_to_wildcard_request_topic(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")

        self.assertIn("mqtt_is_board_publish_schema", main_c)
        for schema in (
            "emqx-gateway.realboard.telemetry.v1",
            "emqx-gateway.realboard.event.v1",
            "emqx-gateway.debug.v1",
            "emqx-gateway.status.v1",
            "emqx-gateway.response.v1",
        ):
            self.assertIn(schema, main_c)
        self.assertRegex(
            main_c,
            r"legacy_payload_envelope\s*=\s*mqtt_is_legacy_payload_envelope[\s\S]+"
            r"if\s*\(\s*mqtt_is_board_publish_schema\s*\(\s*command_root\s*\)\s*\)[\s\S]+"
            r"mqtt_delete_command_roots\s*\(\s*root\s*,\s*command_root\s*\)",
        )

    def test_real_board_ignores_legacy_response_payload_loop_on_single_topic(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")

        self.assertIn("mqtt_is_legacy_response_payload_envelope", main_c)
        self.assertIn("MQTT_LEGACY_RESPONSE_FRM_TYPE 0xA5U", main_c)
        self.assertIn("MQTT_LEGACY_SYNC_RESPONSE_CMD 0x81U", main_c)
        self.assertRegex(
            main_c,
            r"legacy_payload_envelope\s*=\s*mqtt_is_legacy_payload_envelope[\s\S]+"
            r"if\s*\(\s*legacy_payload_envelope\s*&&\s*mqtt_is_legacy_response_payload_envelope\s*\(\s*command_root\s*\)\s*\)[\s\S]+"
            r"mqtt_delete_command_roots\s*\(\s*root\s*,\s*command_root\s*\)[\s\S]+"
            r"real_board_business_handle_legacy_command",
        )

    def test_modbus_tcp_scan_path_accumulates_reads_and_separates_save_reset_coils(self):
        port_c = (REAL_PROJECT / "modbus" / "nmbs" / "port.c").read_text(encoding="utf-8")
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")
        w5500_c = (REAL_SRC / "task_w5500.c").read_text(encoding="utf-8")
        wiz_interface_c = (REAL_SRC / "wiz_interface.c").read_text(encoding="utf-8")

        self.assertIn("wizchip_init(NULL, NULL)", wiz_interface_c)
        self.assertIn("uint16_t total = 0", port_c)
        self.assertIn("getSn_RX_RSR(MB_SOCKET)", port_c)
        self.assertIn("total += (uint16_t)ret", port_c)
        self.assertIn("return (int32_t)total", port_c)
        self.assertIn("nmbs_bitfield_read(nmbs_server.coils, 0)", main_c)
        self.assertIn("nmbs_bitfield_read(nmbs_server.coils, 1)", main_c)
        self.assertNotIn("switch (nmbs_server.coils[0])", main_c)
        for state in ("SOCK_FIN_WAIT", "SOCK_CLOSING", "SOCK_TIME_WAIT", "SOCK_LAST_ACK"):
            self.assertIn(state, w5500_c)
        self.assertIn("close(w5500_socket->sn);", w5500_c)

    def test_a5_peripheral_downlink_has_pending_response_queue(self):
        business_h = (REAL_SRC / "real_board_business.h").read_text(encoding="utf-8")
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")

        self.assertIn("REAL_BOARD_A5_PENDING_MAX", business_h)
        self.assertIn("real_board_a5_pending_t", business_c)
        self.assertIn("real_board_business_handle_peripheral_response", business_h)
        self.assertIn("real_board_business_handle_peripheral_response", business_c)
        self.assertIn('"pending"', business_c)
        self.assertIn('"timeout"', business_c)
        self.assertIn('"response"', business_c)

    def test_real_board_a5_rs485_restores_legacy_busy_queue_and_cmd_matched_response(self):
        hardware_c = (REAL_SRC / "real_board_hardware.c").read_text(encoding="utf-8")
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")

        self.assertIn("#define REAL_BOARD_RS485_QUEUE_DEPTH 4U", hardware_c)
        self.assertIn("#define REAL_BOARD_RS485_QUEUE_PAYLOAD_MAX 128U", hardware_c)
        self.assertIn("#define REAL_BOARD_RS485_RESPONSE_TIMEOUT_MS 1000U", hardware_c)
        self.assertIn("real_board_rs485_queue_item_t", hardware_c)
        self.assertIn("static real_board_rs485_queue_item_t s_rs485_queue[REAL_BOARD_RS485_QUEUE_DEPTH];", hardware_c)
        self.assertIn("static uint8_t s_rs485_active_cmd_type;", hardware_c)
        self.assertIn("static uint8_t real_board_rs485_enqueue(uint8_t cmd_type,", hardware_c)
        self.assertIn("static void real_board_rs485_start_next(void)", hardware_c)
        self.assertIn("static void real_board_rs485_handle_timeout(void)", hardware_c)
        self.assertIn("return real_board_rs485_enqueue(cmd_type, payload, payload_len);", hardware_c)
        self.assertIn("real_board_rs485_enqueue(0x02U, query, sizeof(query));", hardware_c)
        self.assertIn("real_board_business_handle_peripheral_response(s_rs485_active_cmd_type, rx_buf, len);", hardware_c)
        self.assertNotIn("real_board_business_handle_peripheral_response(0x02U, rx_buf, len);", hardware_c)
        self.assertRegex(
            hardware_c,
            r"real_board_rs485_handle_rx\(\);[\s\S]+real_board_rs485_handle_timeout\(\);[\s\S]+real_board_rs485_start_next\(\);",
        )

        self.assertIn("hardware_accepted = real_board_hardware_passthrough_a5", business_c)
        self.assertIn('"outside-device channel busy"', business_c)
        self.assertRegex(
            business_c,
            r"if\s*\(\s*!hardware_accepted\s*\)[\s\S]+\"busy\"[\s\S]+return\s+1;",
        )

    def test_real_v2_legacy_ota_accepts_actual_512_byte_chunks(self):
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")
        ota_h = (REAL_SRC / "real_board_ota.h").read_text(encoding="utf-8")
        ota_c = (REAL_SRC / "real_board_ota.c").read_text(encoding="utf-8")
        raw_max_match = re.search(r"#define\s+REAL_BOARD_RAW_FRAME_MAX\s+(\d+)U", business_c)
        max_chunks = int(re.search(r"#define\s+REAL_BOARD_OTA_MAX_CHUNKS\s+(\d+)U", ota_h).group(1), 10)

        self.assertIsNotNone(raw_max_match)
        raw_max = int(raw_max_match.group(1), 10)
        # FE A5 len + nested FE B2 len + 2-byte index + 512-byte data + two CRC32 fields.
        self.assertGreaterEqual(raw_max, 530)
        self.assertGreaterEqual(max_chunks, 171)
        self.assertIn("ota_pack_flag_set", ota_c)
        self.assertIn("ota_pack_flag_is_set", ota_c)
        self.assertRegex(ota_c, r"pack_flag\s*\[\s*index\s*/\s*8U\s*\]")
        self.assertRegex(ota_c, r"1U\s*<<\s*\(\s*index\s*%\s*8U\s*\)")
        self.assertNotIn("ota_state.pack_flag[index] = 1U", ota_c)

    def test_real_board_parses_connecttype_payload_envelope_and_legacy_a5_nested_ota(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")
        ota_h = (REAL_SRC / "real_board_ota.h").read_text(encoding="utf-8")
        ota_c = (REAL_SRC / "real_board_ota.c").read_text(encoding="utf-8")

        self.assertIn("mqtt_is_legacy_payload_envelope", main_c)
        self.assertIn("mqtt_publish_legacy_payload_envelope_response", main_c)
        for key in ('"connectType"', '"msgType"', '"payload"', '"timestamp"'):
            self.assertIn(key, main_c)
        self.assertRegex(
            main_c,
            r"mqtt_handle_command_json\s*\(\s*payload_in\s*,\s*payloadlen_in\s*,\s*mqtt_received_topic_matches",
        )
        self.assertRegex(
            main_c,
            r"mqtt_publish_legacy_payload_envelope_response[\s\S]+MQTT_TOPIC_EVENT_UP_INDEX",
        )

        self.assertIn("parse_legacy_payload_frame", business_c)
        self.assertIn("build_legacy_payload_frame", business_c)
        self.assertIn("handle_a5_nested_payload_frame", business_c)
        self.assertIn("real_board_ota_handle_legacy_payload", business_c)
        self.assertIn('"frame_hex"', business_c)
        self.assertIn('"msg_hex"', business_c)
        self.assertIn("REAL_BOARD_LEGACY_SYNC_RESPONSE_CMD 0x81U", business_c)

        self.assertIn("real_board_ota_handle_legacy_payload", ota_h)
        for legacy_cmd in ("0xB0U", "0xB1U", "0xB2U", "0xB3U", "0xB4U", "0xB5U", "0xB6U"):
            self.assertIn(f"case {legacy_cmd}:", ota_c)
        self.assertIn("legacy_ota_handle_b2_chunk", ota_c)
        self.assertIn("REAL_BOARD_OTA_LEGACY_CHUNK_INDEX_BYTES", ota_c)
        self.assertIn("write_upgrade_handle();", ota_c)

    def test_real_board_a5_lamp_subprotocol_matches_legacy_frontend_contract(self):
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")

        self.assertIn("REAL_BOARD_LEGACY_HEARTBEAT_PAYLOAD_LEN 50U", business_c)
        self.assertIn("REAL_BOARD_TIMER_CTL_MAX 50U", business_c)
        self.assertIn("real_board_timer_ctl_t", business_c)
        self.assertIn("timer_ctl_1", business_c)
        self.assertIn("timer_ctl_2", business_c)
        for helper in (
            "real_board_set_lamp_brightness",
            "write_legacy_heartbeat_payload",
            "write_legacy_timer_payload",
            "store_legacy_timer_payload",
        ):
            self.assertIn(helper, business_c)

        self.assertRegex(
            business_c,
            r"case\s+0x02U\s*:[\s\S]+REAL_BOARD_LEGACY_HEARTBEAT_PAYLOAD_LEN[\s\S]+write_legacy_heartbeat_payload",
        )
        self.assertRegex(
            business_c,
            r"case\s+0x14U\s*:[\s\S]+write_legacy_timer_payload",
        )
        self.assertRegex(
            business_c,
            r"case\s+0x23U\s*:[\s\S]+store_legacy_timer_payload",
        )
        self.assertRegex(
            business_c,
            r"handle_nested_lamp_control[\s\S]+real_board_set_lamp_brightness",
        )
        self.assertRegex(
            business_c,
            r"append_timer_list[\s\S]+0xCCU[\s\S]+append_timer_list",
        )

    def test_real_board_a5_lamp_control_drives_legacy_hardware_pins(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")
        business_c = (REAL_SRC / "real_board_business.c").read_text(encoding="utf-8")
        conf_h = (REAL_SRC / "hc32f4xx_conf.h").read_text(encoding="utf-8")
        lamp_h = (REAL_SRC / "real_board_lamp.h").read_text(encoding="utf-8")
        lamp_c = (REAL_SRC / "real_board_lamp.c").read_text(encoding="utf-8")
        builder_params = (REAL_PROJECT / "eide" / "build" / "Debug" / "builder.params").read_text(encoding="utf-8")

        self.assertIn("#define LL_TMRA_ENABLE", conf_h)
        self.assertIn('#include "real_board_lamp.h"', main_c)
        self.assertIn("real_board_lamp_init();", main_c)
        self.assertRegex(main_c, r"real_board_lamp_init\(\);[\s\S]+w5500_init\(\);")

        self.assertIn('#include "real_board_lamp.h"', business_c)
        self.assertRegex(
            business_c,
            r"real_board_set_lamp_brightness[\s\S]+Adjust_Lamp\s*\(\s*brightness\s*\)",
        )
        self.assertRegex(
            business_c,
            r"real_board_set_lamp_brightness[\s\S]+Adjust_UNIT4_Lamp\s*\(\s*brightness\s*\)",
        )

        for symbol in (
            "void gpio_init(void);",
            "void real_board_lamp_init(void);",
            "void Adjust_Lamp(uint8_t bright);",
            "void Adjust_UNIT4_Lamp(uint8_t bright);",
        ):
            self.assertIn(symbol, lamp_h)

        for legacy_pin in (
            "#define REAL_BOARD_SWLED1_PORT GPIO_PORT_A",
            "#define REAL_BOARD_SWLED1_PIN GPIO_PIN_12",
            "#define REAL_BOARD_SWLED2_PORT GPIO_PORT_C",
            "#define REAL_BOARD_SWLED2_PIN GPIO_PIN_15",
            "#define REAL_BOARD_RESET_W5500_PORT GPIO_PORT_A",
            "#define REAL_BOARD_RESET_W5500_PIN GPIO_PIN_10",
            "#define REAL_BOARD_LAMP1_PWM_UNIT CM_TMRA_2",
            "#define REAL_BOARD_LAMP1_PWM_CHANNEL TMRA_CH1",
            "#define REAL_BOARD_LAMP1_PWM_PORT GPIO_PORT_A",
            "#define REAL_BOARD_LAMP1_PWM_PIN GPIO_PIN_00",
            "#define REAL_BOARD_LAMP2_PWM_UNIT CM_TMRA_4",
            "#define REAL_BOARD_LAMP2_PWM_CHANNEL TMRA_CH5",
            "#define REAL_BOARD_LAMP2_PWM_PORT GPIO_PORT_C",
            "#define REAL_BOARD_LAMP2_PWM_PIN GPIO_PIN_14",
        ):
            self.assertIn(legacy_pin, lamp_c)

        for pwm_call in (
            "GPIO_SetFunc(REAL_BOARD_LAMP1_PWM_PORT, REAL_BOARD_LAMP1_PWM_PIN, REAL_BOARD_LAMP_PWM_FUNC);",
            "GPIO_SetFunc(REAL_BOARD_LAMP2_PWM_PORT, REAL_BOARD_LAMP2_PWM_PIN, REAL_BOARD_LAMP_PWM_FUNC);",
            "TMRA_PWM_Init(",
            "TMRA_SetCompareValue(",
            "TMRA_Start(",
        ):
            self.assertIn(pwm_call, lamp_c)

        self.assertIn("GPIO_SetPins(REAL_BOARD_SWLED1_PORT, REAL_BOARD_SWLED1_PIN);", lamp_c)
        self.assertIn("GPIO_ResetPins(REAL_BOARD_SWLED1_PORT, REAL_BOARD_SWLED1_PIN);", lamp_c)
        self.assertIn("GPIO_SetPins(REAL_BOARD_SWLED2_PORT, REAL_BOARD_SWLED2_PIN);", lamp_c)
        self.assertIn("GPIO_ResetPins(REAL_BOARD_SWLED2_PORT, REAL_BOARD_SWLED2_PIN);", lamp_c)
        self.assertIn("../src/real_board_lamp.c", builder_params)

    def test_real_board_hardware_acquisition_matches_legacy_uart_and_meter_paths(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")
        conf_h = (REAL_SRC / "hc32f4xx_conf.h").read_text(encoding="utf-8")
        usart_h = (REAL_PROJECT / "driver" / "usart.h").read_text(encoding="utf-8")
        usart_c = (REAL_PROJECT / "driver" / "usart.c").read_text(encoding="utf-8")
        hardware_h = (REAL_SRC / "real_board_hardware.h").read_text(encoding="utf-8")
        hardware_c = (REAL_SRC / "real_board_hardware.c").read_text(encoding="utf-8")
        builder_params = (REAL_PROJECT / "eide" / "build" / "Debug" / "builder.params").read_text(encoding="utf-8")

        self.assertIn("#define LL_USART_ENABLE", conf_h)
        self.assertIn('#include "real_board_hardware.h"', main_c)
        self.assertIn("real_board_hardware_init();", main_c)
        self.assertIn("real_board_hardware_task();", main_c)
        self.assertRegex(main_c, r"real_board_hardware_task\(\);[\s\S]+real_board_business_tick\(\);")

        for symbol in (
            "void real_board_hardware_init(void);",
            "void real_board_hardware_task(void);",
            "void real_board_hlw8112_init_enable(void);",
            "uint8_t real_board_rs485_send(uint8_t *data, uint16_t len);",
        ):
            self.assertIn(symbol, hardware_h)

        for legacy_uart_pin in (
            "#define REAL_BOARD_USART1_TX_PORT GPIO_PORT_B",
            "#define REAL_BOARD_USART1_TX_PIN GPIO_PIN_00",
            "#define REAL_BOARD_USART1_RX_PORT GPIO_PORT_B",
            "#define REAL_BOARD_USART1_RX_PIN GPIO_PIN_01",
            "#define REAL_BOARD_USART2_TX_PORT GPIO_PORT_A",
            "#define REAL_BOARD_USART2_TX_PIN GPIO_PIN_02",
            "#define REAL_BOARD_USART2_RX_PORT GPIO_PORT_A",
            "#define REAL_BOARD_USART2_RX_PIN GPIO_PIN_03",
            "#define REAL_BOARD_USART3_RX_PORT GPIO_PORT_B",
            "#define REAL_BOARD_USART3_RX_PIN GPIO_PIN_10",
            "#define REAL_BOARD_USART3_TX_PORT GPIO_PORT_B",
            "#define REAL_BOARD_USART3_TX_PIN GPIO_PIN_12",
        ):
            self.assertIn(legacy_uart_pin, usart_c)

        self.assertNotIn("GPIO_SetFunc(GPIO_PORT_A,GPIO_PIN_04", usart_c)
        self.assertNotIn("GPIO_SetFunc(GPIO_PORT_A,GPIO_PIN_05", usart_c)
        self.assertIn("void usart2_init(void);", usart_h)
        self.assertIn("void usart3_init(void);", usart_h)
        self.assertIn("uint16_t usart2_read_available(uint8_t *data, uint16_t max_len);", usart_h)
        self.assertIn("uint16_t usart3_read_available(uint8_t *data, uint16_t max_len);", usart_h)
        self.assertIn("usart2_init();", hardware_c)
        self.assertIn("usart3_init();", hardware_c)

        for meter_token in (
            "REAL_BOARD_HLW8112_REG_UFREQ 0x23U",
            "REAL_BOARD_HLW8112_REG_RMS_IA 0x24U",
            "REAL_BOARD_HLW8112_REG_RMS_U 0x26U",
            "REAL_BOARD_HLW8112_REG_POWER_FACTOR 0x27U",
            "REAL_BOARD_HLW8112_REG_ENERGY_PA 0x28U",
            "REAL_BOARD_HLW8112_REG_POWER_PA 0x2CU",
            "REAL_BOARD_HLW8112_REG_POWER_S 0x2EU",
            "REAL_BOARD_HLW8112_REG_RMS_IAC 0x70U",
            "REAL_BOARD_HLW8112_REG_RMS_UC 0x72U",
            "REAL_BOARD_HLW8112_REG_POWER_PAC 0x73U",
            "REAL_BOARD_HLW8112_REG_POWER_SC 0x75U",
            "REAL_BOARD_HLW8112_REG_ENERGY_PAC 0x76U",
            "REAL_BOARD_HLW8112_REG_HFCONST 0x02U",
        ):
            self.assertIn(meter_token, hardware_c)

        self.assertIn("usart3_write(frame, len);", hardware_c)
        self.assertIn("usart3_read_available(rx_buf, sizeof(rx_buf));", hardware_c)
        self.assertIn("real_board_business_update_meter_hlw8112(&meter);", hardware_c)
        self.assertIn("real_board_business_update_environment(&environment);", hardware_c)
        self.assertIn("real_board_business_update_rs485(&rs485);", hardware_c)
        self.assertIn("GPIO_SetPins(REAL_BOARD_RS485_EN_PORT, REAL_BOARD_RS485_EN_PIN);", hardware_c)
        self.assertIn("GPIO_ResetPins(REAL_BOARD_RE_485_1_PORT, REAL_BOARD_RE_485_1_PIN);", hardware_c)
        self.assertIn("../src/real_board_hardware.c", builder_params)

    def test_hlw8112_real_uart_response_accumulates_chunks_and_recovers_on_timeout(self):
        hardware_c = (REAL_SRC / "real_board_hardware.c").read_text(encoding="utf-8")

        self.assertIn("#define REAL_BOARD_HLW8112_RX_BUFFER_MAX 8U", hardware_c)
        self.assertIn("#define REAL_BOARD_HLW8112_RESPONSE_TIMEOUT_MS 5000U", hardware_c)
        self.assertIn("uint8_t rx_len;", hardware_c)
        self.assertIn("uint8_t rx_buf[REAL_BOARD_HLW8112_RX_BUFFER_MAX];", hardware_c)
        self.assertIn("s_hlw8112.rx_len", hardware_c)
        self.assertIn("static void real_board_hlw8112_handle_timeout(void)", hardware_c)
        self.assertRegex(
            hardware_c,
            r"real_board_hlw8112_handle_rx[\s\S]+memcpy\(&s_hlw8112\.rx_buf\[s_hlw8112\.rx_len\]",
        )
        self.assertRegex(
            hardware_c,
            r"real_board_hlw8112_read_task[\s\S]+s_hlw8112\.awaiting_order\s*=\s*s_hlw8112\.reg_order;[\s\S]+"
            r"s_hlw8112\.pending_tick\s*=\s*g_real_board_millis;[\s\S]+s_hlw8112\.read_open\s*=\s*0U;",
        )
        self.assertRegex(
            hardware_c,
            r"real_board_hardware_task[\s\S]+real_board_hlw8112_read_task\(\);[\s\S]+real_board_hlw8112_handle_timeout\(\);",
        )

    def test_tls_dual_auth_and_ntp_state_machine_are_integrated(self):
        main_c = (REAL_SRC / "main.c").read_text(encoding="utf-8")
        config_h = (REAL_SRC / "config.h").read_text(encoding="utf-8")
        config_c = (REAL_SRC / "config.c").read_text(encoding="utf-8")
        builder_params = (REAL_PROJECT / "eide" / "build" / "Debug" / "builder.params").read_text(encoding="utf-8")

        tls_h = (REAL_SRC / "real_board_tls.h").read_text(encoding="utf-8")
        tls_c = (REAL_SRC / "real_board_tls.c").read_text(encoding="utf-8")
        ntp_h = (REAL_SRC / "real_board_ntp.h").read_text(encoding="utf-8")
        ntp_c = (REAL_SRC / "real_board_ntp.c").read_text(encoding="utf-8")

        self.assertIn("tls_mode", config_h)
        self.assertIn("REAL_BOARD_TLS_MODE_MUTUAL", tls_h)
        self.assertIn("real_board_tls_is_enabled", tls_h)
        self.assertIn("real_board_tls_setup", tls_h)
        self.assertIn("MBEDTLS_SSL_VERIFY_REQUIRED", tls_c)
        self.assertIn("mbedtls_x509_crt_parse", tls_c)
        self.assertIn("mbedtls_pk_parse_key", tls_c)
        self.assertIn("mbedtls_ssl_conf_own_cert", tls_c)
        self.assertIn("real_board_tls_is_enabled(system_config)", main_c)

        self.assertIn("real_board_ntp_init", ntp_h)
        self.assertIn("real_board_ntp_task", ntp_h)
        self.assertIn("real_board_ntp_append_status", ntp_h)
        self.assertIn("SNTP_init", ntp_c)
        self.assertIn("SNTP_run", ntp_c)
        self.assertIn('"ntp"', ntp_c)
        self.assertIn("real_board_ntp_task();", main_c)
        self.assertIn("real_board_ntp_append_status(data);", main_c)
        self.assertIn("REAL_BOARD_DEFAULT_NTP_SERVER", config_c)
        self.assertIn("REAL_BOARD_NTP_DNS_RETRY_TICKS", ntp_c)
        self.assertIn("NTP DNS deferred", ntp_c)
        self.assertNotIn("w5500_dns(&dns_socket", ntp_c)

        self.assertIn("../src/real_board_tls.c", builder_params)
        self.assertIn("../src/real_board_ntp.c", builder_params)
        self.assertIn("../ioLibrary_Driver-3.2.0/Internet/SNTP/sntp.c", builder_params)


if __name__ == "__main__":
    unittest.main()
