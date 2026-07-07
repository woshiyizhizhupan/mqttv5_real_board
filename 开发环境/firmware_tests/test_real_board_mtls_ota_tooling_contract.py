import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class RealBoardMtlsOtaToolingContractTests(unittest.TestCase):
    def test_certificate_generator_accepts_lan_sans_and_writes_manifest(self):
        text = (ROOT / "开发环境" / "emqx_local" / "generate_local_mtls_certs.py").read_text(encoding="utf-8")

        self.assertIn("--server-ip", text)
        self.assertIn("--server-dns", text)
        self.assertIn("manifest.json", text)
        self.assertIn("SubjectAlternativeName", text)

    def test_certificate_generator_uses_hc32_mbedtls_friendly_ca(self):
        import re

        text = (ROOT / "开发环境" / "emqx_local" / "generate_local_mtls_certs.py").read_text(encoding="utf-8")
        ca_body = re.search(r"def _build_ca[\s\S]+?\n\ndef _build_leaf", text)

        self.assertIsNotNone(ca_body)
        self.assertIn("HC32_MBEDTLS_COMPAT_CA", ca_body.group(0))
        self.assertIn("x509.BasicConstraints(ca=True, path_length=None)", ca_body.group(0))
        self.assertNotIn("path_length=1", ca_body.group(0))
        self.assertNotIn("x509.KeyUsage", ca_body.group(0))
        self.assertIn(".sign(private_key, hashes.SHA256())", ca_body.group(0))

    def test_emqx_mtls_listener_uses_board_friendly_handshake_timeout(self):
        text = (ROOT / "开发环境" / "emqx_local" / "Enable-LocalEMQX-MTLS.ps1").read_text(encoding="utf-8")

        self.assertIn("HandshakeTimeout", text)
        self.assertIn('"120s"', text)
        self.assertIn('"192.168.0.31"', text)
        self.assertIn("handshake_timeout = $HandshakeTimeout", text)

    def test_real_board_build_can_embed_mtls_certs_and_select_legacy_test_path(self):
        ps1 = (ROOT / "开发环境" / "scripts" / "Build-RealBoard-Firmware.ps1").read_text(encoding="utf-8")
        embed = ROOT / "开发环境" / "scripts" / "embed_real_board_mtls_certs.py"

        self.assertTrue(embed.exists(), "embed_real_board_mtls_certs.py must exist")
        self.assertIn("MtlsCertDir", ps1)
        self.assertIn("DisableLegacyFrame", ps1)
        self.assertIn("REAL_BOARD_ENABLE_LEGACY_FRAME=1", ps1)
        self.assertIn("REAL_BOARD_ENABLE_LEGACY_FRAME=0", ps1)
        self.assertIn("REAL_BOARD_OTA_REBOOT_AFTER_END=0", ps1)
        self.assertIn("REAL_BOARD_FW_VERSION", ps1)

    def test_stage1_mtls_provision_script_generates_embeds_builds_and_safe_flashes(self):
        script = ROOT / "开发环境" / "scripts" / "Provision-RealBoardMTLSFirmware.ps1"
        self.assertTrue(script.exists(), "Provision-RealBoardMTLSFirmware.ps1 must exist")
        text = script.read_text(encoding="utf-8")

        self.assertIn("[string]$BoardIp", text)
        self.assertIn("[Parameter(Mandatory = $true)]", text)
        self.assertIn("[string]$ServerIp", text)
        self.assertIn("generate_local_mtls_certs.py", text)
        self.assertIn("--server-ip", text)
        self.assertIn("--server-dns", text)
        self.assertIn("--client-common-name", text)
        self.assertIn("Build-RealBoard-Firmware.ps1", text)
        self.assertIn("-MtlsCertDir", text)
        self.assertIn("REAL_BOARD_DEFAULT_MQTT_HOST", text)
        self.assertIn("REAL_BOARD_DEFAULT_MQTT_PORT", text)
        self.assertIn("REAL_BOARD_DEFAULT_TLS_MODE=2", text)
        self.assertIn("REAL_BOARD_DEFAULT_TLS_VERIFY_PEER=1", text)
        self.assertIn("SYSTEM_CONFIG_VERSION", text)
        self.assertIn("Flash-MQTTv5-STLink.ps1", text)
        self.assertIn("-Image", text)
        self.assertNotIn("hc32_openocd_flash_mqttv5.generated.tcl", text)
        self.assertNotIn("0x00000000", text)

    def test_stage1_mtls_provision_script_uses_board_ip_for_probe_and_config_not_cert_san(self):
        script = ROOT / "开发环境" / "scripts" / "Provision-RealBoardMTLSFirmware.ps1"
        self.assertTrue(script.exists(), "Provision-RealBoardMTLSFirmware.ps1 must exist")
        text = script.read_text(encoding="utf-8")

        self.assertIn("host_tool_config_probe.csproj", text)
        self.assertIn("Resolve-DeviceIdFromBoard", text)
        self.assertIn("write-remote-emqx", text)
        self.assertIn("SkipBoardProbe", text)
        self.assertIn("SkipPostFlashConfig", text)
        self.assertIn("SkipFlash", text)
        self.assertRegex(text, r'--server-ip",\s*\$ServerIp')
        self.assertNotRegex(text, r'--server-ip",\s*\$BoardIp')

    def test_readback_script_only_dumps_app2_window(self):
        readback = ROOT / "开发环境" / "scripts" / "Read-RealBoardApp2.ps1"
        self.assertTrue(readback.exists(), "Read-RealBoardApp2.ps1 must exist")
        text = readback.read_text(encoding="utf-8")

        self.assertIn("0x00042000", text)
        self.assertIn("0x00076000", text)
        self.assertIn("real_board_app2_readback_openocd_", text)
        self.assertIn("Copy-Item -LiteralPath $openOcdOutput -Destination $Output -Force", text)
        self.assertIn("dump_image", text)
        self.assertNotIn("flash write", text.lower())
        self.assertNotIn("flash erase", text.lower())

    def test_flash_script_prefers_bundled_openocd_and_flm_tools(self):
        script = ROOT / "开发环境" / "scripts" / "Flash-MQTTv5-STLink.ps1"
        self.assertTrue(script.exists(), "Flash-MQTTv5-STLink.ps1 must exist")
        text = script.read_text(encoding="utf-8")

        self.assertIn("function Resolve-ProjectRoot", text)
        self.assertIn('"06_scripts"', text)
        self.assertIn('Join-Path $PSScriptRoot "generated"', text)
        self.assertIn("[System.IO.Path]::GetTempPath()", text)
        self.assertIn("tools\\openocd\\xpack-openocd-0.12.0-7\\bin\\openocd.exe", text)
        self.assertIn("tools\\hdsc_flm\\HC32F460_512K_prg.bin", text)
        self.assertLess(
            text.index("tools\\openocd\\xpack-openocd-0.12.0-7\\bin\\openocd.exe"),
            text.index("D:\\Tools\\xpack-openocd-0.12.0-7\\bin\\openocd.exe"),
        )
        self.assertLess(
            text.index("tools\\hdsc_flm\\HC32F460_512K_prg.bin"),
            text.index("D:\\Tools\\hdsc-pack-inspect\\HC32F460_512K_prg.bin"),
        )

    def test_test_firmware_can_override_default_mqtt_mtls_config(self):
        config_h = (ROOT / "mqttv5_real_board" / "src" / "config.h").read_text(encoding="utf-8")
        config_c = (ROOT / "mqttv5_real_board" / "src" / "config.c").read_text(encoding="utf-8")

        self.assertIn("#ifndef SYSTEM_CONFIG_VERSION", config_h)
        self.assertIn("REAL_BOARD_DEFAULT_MQTT_HOST", config_c)
        self.assertIn("REAL_BOARD_DEFAULT_MQTT_PORT", config_c)
        self.assertIn("REAL_BOARD_DEFAULT_TLS_MODE", config_c)
        self.assertIn("REAL_BOARD_DEFAULT_TLS_VERIFY_PEER", config_c)

    def test_tls_mode_infers_mutual_tls_for_8884_legacy_configs(self):
        tls_c = (ROOT / "mqttv5_real_board" / "src" / "real_board_tls.c").read_text(encoding="utf-8")

        self.assertRegex(tls_c, r"port\s*==\s*8884U[\s\S]+REAL_BOARD_TLS_MODE_MUTUAL")
        self.assertRegex(tls_c, r"port\s*==\s*8883U[\s\S]+REAL_BOARD_TLS_MODE_SERVER_AUTH")

    def test_end_to_end_validation_script_records_mqtt_and_flash_compare(self):
        script = ROOT / "开发环境" / "scripts" / "Run-RealBoardMtlsOtaValidation.ps1"
        tester = ROOT / "开发环境" / "host_mqtt_tester" / "mqtt_tester" / "sim_board_server.py"
        self.assertTrue(script.exists(), "Run-RealBoardMtlsOtaValidation.ps1 must exist")
        text = script.read_text(encoding="utf-8")
        tester_text = tester.read_text(encoding="utf-8")

        self.assertIn("mqtt_tester.sim_board_server", text)
        self.assertIn("ota-send", text)
        self.assertIn("--timeout", text)
        self.assertIn("--request-interval", text)
        self.assertIn("--retries", text)
        self.assertIn("Read-RealBoardApp2.ps1", text)
        self.assertIn("Compare-Object", text)
        self.assertIn("mtls_ota_test", text)
        self.assertIn("def _request_with_retries", tester_text)
        self.assertIn("ota.add_argument(\"--request-interval\"", tester_text)
        self.assertIn("ota.add_argument(\"--retries\"", tester_text)

    def test_mqtt_tls_failures_return_to_main_loop_instead_of_deadlocking(self):
        main_c = (ROOT / "mqttv5_real_board" / "src" / "main.c").read_text(encoding="utf-8")

        self.assertIn("TLS_HANDSHAKE_MAX_ATTEMPTS", main_c)
        self.assertIn("volatile int32_t mqtt_last_tls_error", main_c)
        self.assertIn("volatile uint8_t mqtt_last_tls_stage", main_c)
        self.assertIn("mbedtls_entropy_add_source(&entropy, mbedtls_hardware_poll", main_c)
        self.assertIn("MBEDTLS_ENTROPY_SOURCE_STRONG", main_c)
        self.assertIn("mqtt_tls_cleanup", main_c)
        self.assertIn("mqtt_reset_connection", main_c)
        self.assertIn("close(w5500_socket_mqtt.sn);", main_c)
        self.assertIn("MQTT_STARTUP_DELAY_TICKS", main_c)
        self.assertIn("MQTT_RECONNECT_BACKOFF_TICKS 1000U", main_c)
        self.assertIn("MQTT_STARTUP_DELAY_TICKS 100U", main_c)
        self.assertRegex(main_c, r"mqtt_reconnect_backoff_ticks\s*=\s*MQTT_STARTUP_DELAY_TICKS")
        self.assertRegex(main_c, r"mqtt_backoff_task[\s\S]+delay_ms\(1\);")
        self.assertRegex(main_c, r"mqtt_backoff_task[\s\S]+mqtt_service_management_channel\(\);")
        self.assertRegex(main_c, r"if\s*\(\s*mqtt_init\(\)\s*==\s*0\s*\)")
        self.assertRegex(main_c, r"if\s*\(\s*mqtt_connect\(\)\s*==\s*0\s*\)")

        for marker in (
            "SSL initialization failed",
            "mqtt_send failed",
            "Unable to connect",
            "Failed to read CONNACK",
        ):
            self.assertNotRegex(
                main_c,
                marker + r'[\s\S]{0,160}while\s*\(\s*1\s*\)',
                f"{marker} path must not stop the board main loop",
            )

    def test_tls_setup_records_substage_for_certificate_parse_failures(self):
        tls_c = (ROOT / "mqttv5_real_board" / "src" / "real_board_tls.c").read_text(encoding="utf-8")
        tls_h = (ROOT / "mqttv5_real_board" / "src" / "real_board_tls.h").read_text(encoding="utf-8")

        self.assertIn("volatile int32_t real_board_tls_last_error", tls_c)
        self.assertIn("volatile uint8_t real_board_tls_last_step", tls_c)
        self.assertIn("extern volatile int32_t real_board_tls_last_error", tls_h)
        self.assertIn("extern volatile uint8_t real_board_tls_last_step", tls_h)
        self.assertIn("REAL_BOARD_TLS_STEP_PARSE_CA", tls_c)
        self.assertIn("REAL_BOARD_TLS_STEP_PARSE_CLIENT_CERT", tls_c)
        self.assertIn("REAL_BOARD_TLS_STEP_PARSE_CLIENT_KEY", tls_c)
        self.assertRegex(tls_c, r"real_board_tls_last_step\s*=\s*REAL_BOARD_TLS_STEP_PARSE_CA[\s\S]+mbedtls_x509_crt_parse")
        self.assertRegex(tls_c, r"real_board_tls_last_step\s*=\s*REAL_BOARD_TLS_STEP_PARSE_CLIENT_CERT[\s\S]+mbedtls_x509_crt_parse")
        self.assertRegex(tls_c, r"real_board_tls_last_step\s*=\s*REAL_BOARD_TLS_STEP_PARSE_CLIENT_KEY[\s\S]+mbedtls_pk_parse_key")
        self.assertRegex(tls_c, r"real_board_tls_last_error\s*=\s*ret")

    def test_tls_certificate_lengths_are_computed_at_runtime(self):
        tls_c = (ROOT / "mqttv5_real_board" / "src" / "real_board_tls.c").read_text(encoding="utf-8")

        self.assertIn("real_board_tls_pem_len_with_nul", tls_c)
        self.assertIn("const volatile char *cursor", tls_c)
        self.assertIn("real_board_tls_pem_is_empty", tls_c)
        self.assertNotIn("strlen(REAL_BOARD_TLS_CA_CERT_PEM) + 1U", tls_c)
        self.assertNotIn("strlen(REAL_BOARD_TLS_CLIENT_CERT_PEM) + 1U", tls_c)
        self.assertNotIn("strlen(REAL_BOARD_TLS_CLIENT_KEY_PEM) + 1U", tls_c)

    def test_x509_parser_exposes_minimal_board_diagnostics(self):
        x509_crt_c = (ROOT / "mqttv5_real_board" / "mbedtls" / "library" / "x509_crt.c").read_text(encoding="utf-8")

        self.assertIn("volatile int32_t real_board_x509_last_pem_ret", x509_crt_c)
        self.assertIn("volatile int32_t real_board_x509_last_der_ret", x509_crt_c)
        self.assertIn("volatile uint32_t real_board_x509_last_pem_use_len", x509_crt_c)
        self.assertIn("volatile uint32_t real_board_x509_last_der_len", x509_crt_c)
        self.assertIn("volatile uint8_t real_board_x509_last_format", x509_crt_c)
        self.assertRegex(x509_crt_c, r"real_board_x509_last_format\s*=\s*1U[\s\S]+MBEDTLS_X509_FORMAT_PEM")
        self.assertRegex(x509_crt_c, r"real_board_x509_last_pem_ret\s*=\s*ret")
        self.assertRegex(x509_crt_c, r"real_board_x509_last_der_ret\s*=\s*ret")

    def test_mqtt_tls_waiting_services_modbus_management_socket(self):
        main_c = (ROOT / "mqttv5_real_board" / "src" / "main.c").read_text(encoding="utf-8")

        self.assertIn("mqtt_service_management_channel", main_c)
        self.assertIn("w5500_tcp_server(&w5500_socket_modbus);", main_c)
        self.assertIn("modbus_task();", main_c)
        self.assertRegex(
            main_c,
            r"socket_recv[\s\S]+mqtt_service_management_channel\(\);",
        )
        self.assertRegex(
            main_c,
            r"mbedtls_ssl_handshake[\s\S]+mqtt_service_management_channel\(\);",
        )

    def test_tls_socket_recv_reads_available_bytes_without_waiting_for_full_mbedtls_buffer(self):
        main_c = (ROOT / "mqttv5_real_board" / "src" / "main.c").read_text(encoding="utf-8")

        self.assertNotIn("if (size <= len)", main_c)
        self.assertIn("uint16_t read_len = size < len ? size : (uint16_t)len;", main_c)
        self.assertIn("recv(sock_fd, (uint8_t *)buf, read_len)", main_c)
        self.assertIn("#define TLS_HANDSHAKE_MAX_ATTEMPTS 5000U", main_c)

    def test_newlib_sbrk_is_bounded_before_tls_dynamic_allocations(self):
        syscalls_c = (ROOT / "mqttv5_real_board" / "src" / "syscalls.c").read_text(encoding="utf-8")

        self.assertIn("extern char __HeapLimit", syscalls_c)
        self.assertIn("ENOMEM", syscalls_c)
        self.assertRegex(syscalls_c, r"heap\s*\+\s*incr\s*>\s*\(unsigned char \*\)&__HeapLimit")
        self.assertRegex(syscalls_c, r"return\s+\(caddr_t\)-1")

    def test_tls_build_reserves_heap_large_enough_for_mbedtls_buffers(self):
        startup_s = (ROOT / "mqttv5_real_board" / "libraries" / "device" / "startup_hc32f460.S").read_text(encoding="utf-8")
        match = __import__("re").search(r"\.equ\s+Heap_Size,\s+0x([0-9A-Fa-f]+)", startup_s)

        self.assertIsNotNone(match, "GCC startup must define Heap_Size")
        self.assertGreaterEqual(int(match.group(1), 16), 0x00010000)

    def test_mqtt_backoff_does_not_drive_socket0_client_path(self):
        main_c = (ROOT / "mqttv5_real_board" / "src" / "main.c").read_text(encoding="utf-8")
        task_w5500 = (ROOT / "mqttv5_real_board" / "src" / "task_w5500.c").read_text(encoding="utf-8")
        task_w5500_h = (ROOT / "mqttv5_real_board" / "src" / "task_w5500.h").read_text(encoding="utf-8")

        self.assertIn("void w5500_modbus_task();", task_w5500_h)
        self.assertIn("void w5500_mqtt_client_task();", task_w5500_h)
        self.assertIn("void w5500_modbus_task()", task_w5500)
        self.assertIn("void w5500_mqtt_client_task()", task_w5500)
        self.assertIn("w5500_modbus_task();", main_c)
        self.assertRegex(
            main_c,
            r"if\s*\(\s*!mqtt_backoff_task\(\)\s*\)\s*\{[\s\S]{0,220}w5500_mqtt_client_task\(\);",
        )
        self.assertNotRegex(
            main_c,
            r"while\s*\(\s*1\s*\)[\s\S]{0,120}w5500_task\(\);[\s\S]{0,180}mqtt_backoff_task\(\)",
        )

    def test_modbus_tcp_server_recovers_idle_probe_connections(self):
        task_w5500 = (ROOT / "mqttv5_real_board" / "src" / "task_w5500.c").read_text(encoding="utf-8")

        self.assertIn("W5500_TCP_SERVER_IDLE_TICKS", task_w5500)
        self.assertIn("phy_link_was_on", task_w5500)
        self.assertRegex(
            task_w5500,
            r"phy_link_status\s*==\s*PHY_LINK_ON[\s\S]+network_init\(ethernet_buf",
        )
        self.assertNotRegex(
            task_w5500,
            r"phy_link_status\s*!=\s*PHY_LINK_ON[\s\S]{0,220}network_init\(ethernet_buf",
        )
        self.assertRegex(task_w5500, r"W5500_TCP_SERVER_IDLE_TICKS\s+5000U")
        self.assertIn("server_idle_ticks", task_w5500)
        self.assertRegex(task_w5500, r"getSn_RX_RSR\s*\(\s*w5500_socket->sn\s*\)")
        self.assertIn("Sn_IR_DISCON", task_w5500)
        self.assertIn("Sn_IR_TIMEOUT", task_w5500)
        self.assertRegex(
            task_w5500,
            r"server_idle_ticks\s*\[\s*w5500_socket->sn\s*\][\s\S]+disconnect\s*\(\s*w5500_socket->sn\s*\)",
        )
        self.assertRegex(
            task_w5500,
            r"case\s+SOCK_CLOSE_WAIT:[\s\S]+disconnect\s*\(\s*w5500_socket->sn\s*\)[\s\S]+close\s*\(\s*w5500_socket->sn\s*\)",
        )
        self.assertRegex(task_w5500, r"server_idle_ticks\s*\[\s*w5500_socket->sn\s*\]\s*=\s*0")

    def test_modbus_poll_transport_errors_close_management_socket(self):
        main_c = (ROOT / "mqttv5_real_board" / "src" / "main.c").read_text(encoding="utf-8")

        self.assertRegex(main_c, r"nmbs_error\s+poll_result")
        self.assertRegex(main_c, r"poll_result\s*=\s*nmbs_server_poll\s*\(\s*&nmbs\s*\)")
        self.assertRegex(main_c, r"poll_result\s*==\s*NMBS_ERROR_TIMEOUT[\s\S]+return;")
        self.assertRegex(main_c, r"poll_result\s*==\s*NMBS_ERROR_TRANSPORT[\s\S]+close\s*\(\s*w5500_socket_modbus\.sn\s*\)")
        self.assertRegex(main_c, r"poll_result\s*==\s*NMBS_ERROR_INVALID_TCP_MBAP[\s\S]+close\s*\(\s*w5500_socket_modbus\.sn\s*\)")


if __name__ == "__main__":
    unittest.main()
