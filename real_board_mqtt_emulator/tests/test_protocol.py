import sys
import unittest
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT))

from real_board_emulator.protocol import BoardConfig, RealBoardProtocol
from real_board_emulator.topics import build_city_device_topics


class RealBoardProtocolTests(unittest.TestCase):
    def test_default_topics_match_real_board_city_contract(self):
        topics = build_city_device_topics("tjw", "pole001", "GM400-452089")

        self.assertEqual("city/tjw/pole/pole001/device/GM400-452089/", topics.telemetry_up)
        self.assertEqual("city/tjw/pole/pole001/device/GM400-452089/get", topics.cmd_down)
        self.assertEqual("city/tjw/pole/pole001/device/GM400-452089/event", topics.event_up)
        self.assertEqual("city/tjw/pole/pole001/device/GM400-452089/ota", topics.ota_down)
        self.assertEqual("city/tjw/pole/pole001/device/GM400-452089/debug", topics.debug_up)
        self.assertEqual([topics.cmd_down, topics.ota_down], topics.downlink_topics())

    def test_telemetry_uses_real_board_schema_and_nested_payloads(self):
        protocol = RealBoardProtocol(BoardConfig.default())
        protocol.advance_simulation(100)

        payload = protocol.build_telemetry_payload()

        self.assertEqual("emqx-gateway.realboard.telemetry.v1", payload["schema"])
        self.assertEqual("GM400-452089", payload["device_id"])
        self.assertEqual("real_board", payload["business_mode"])
        self.assertEqual("real-board-app1", payload["firmware_version"])
        self.assertEqual("real_driver", payload["meter_hlw8112"]["source"])
        self.assertEqual("real_driver", payload["environment"]["source"])
        self.assertTrue(payload["meter_hlw8112"]["valid"])
        self.assertTrue(payload["environment"]["valid"])
        self.assertTrue(payload["rs485"]["valid"])
        self.assertIn("lighting", payload)
        self.assertIn("factory_test", payload)
        self.assertIn("a5_pending", payload["legacy_protocol"])
        self.assertFalse(payload["legacy_protocol"]["downlink_supported"])

    def test_ping_status_and_config_commands_publish_real_responses(self):
        protocol = RealBoardProtocol(BoardConfig.default())

        ping = protocol.handle_command_dict({"id": "req-ping", "cmd": "ping"})[0].payload
        self.assertEqual("emqx-gateway.response.v1", ping["schema"])
        self.assertEqual("req-ping", ping["id"])
        self.assertEqual("ping", ping["cmd"])
        self.assertTrue(ping["ok"])
        self.assertEqual(0, ping["code"])
        self.assertEqual({"pong": True}, ping["data"])

        status = protocol.handle_command_dict({"id": "req-status", "cmd": "get_status"})[0].payload
        self.assertEqual("get_status", status["cmd"])
        self.assertTrue(status["ok"])
        self.assertEqual("connected", status["data"]["mqtt_state"])
        self.assertEqual("real_board", status["data"]["business_mode"])
        self.assertIn("ota", status["data"])
        self.assertIn("ntp", status["data"])

        config = protocol.handle_command_dict({"id": "req-config", "cmd": "get_config"})[0].payload
        self.assertEqual("get_config", config["cmd"])
        self.assertEqual("GM400-452089", config["data"]["device_id"])
        self.assertEqual("39.103.154.108", config["data"]["host"])
        self.assertFalse(config["data"]["password_set"])
        self.assertEqual(5, len(config["data"]["topics"]))
        self.assertEqual([2, 2, 2, 2, 2], config["data"]["qos"])

    def test_real_set_updates_control_state_and_ignores_telemetry_fields(self):
        protocol = RealBoardProtocol(BoardConfig.default())

        response = protocol.handle_command_dict(
            {
                "id": "req-set",
                "cmd": "real_set",
                "params": {
                    "relay1_on": True,
                    "relay2_on": False,
                    "lamp1_brightness": 88,
                    "voltage_mv": 221000,
                    "rs485_online": False,
                },
            }
        )[0].payload

        self.assertTrue(response["ok"])
        self.assertEqual("real board state updated", response["message"])
        self.assertEqual("real_board", response["data"]["mode"])
        self.assertFalse(response["data"]["telemetry_fields_written"])
        self.assertIn("voltage_mv", response["data"]["ignored_real_only_fields"])
        self.assertIn("rs485_online", response["data"]["ignored_real_only_fields"])

        telemetry = protocol.build_telemetry_payload()
        self.assertTrue(telemetry["lighting"]["relay1_on"])
        self.assertFalse(telemetry["lighting"]["relay2_on"])
        self.assertEqual(88, telemetry["lighting"]["lamp1_brightness"])

    def test_real_event_queues_alarm_payload_for_event_topic(self):
        protocol = RealBoardProtocol(BoardConfig.default())

        response = protocol.handle_command_dict(
            {"id": "req-event", "cmd": "real_event", "params": {"event": "over_current", "level": "alarm"}}
        )[0].payload
        self.assertTrue(response["ok"])
        self.assertEqual("over_current", response["data"]["queued_event"])
        self.assertEqual("alarm", response["data"]["level"])

        event = protocol.pop_queued_event_payload()
        self.assertIsNotNone(event)
        self.assertEqual("emqx-gateway.realboard.event.v1", event["schema"])
        self.assertEqual("over_current", event["event"])
        self.assertEqual("alarm", event["level"])
        self.assertIn("meter_hlw8112_valid", event["data"])

        self.assertIsNone(protocol.pop_queued_event_payload())

    def test_ota_status_and_begin_are_stateful(self):
        protocol = RealBoardProtocol(BoardConfig.default())

        status = protocol.handle_command_dict({"id": "ota-status-1", "cmd": "ota_status"})[0].payload
        self.assertTrue(status["ok"])
        self.assertFalse(status["data"]["active"])
        self.assertEqual(0x00004000, status["data"]["app1_start"])
        self.assertEqual(0x00042000, status["data"]["app2_start"])
        self.assertEqual(0x0007A000, status["data"]["upgrade_state_addr"])

        begin = protocol.handle_command_dict(
            {
                "id": "ota-begin-1",
                "cmd": "ota_begin",
                "params": {
                    "session_id": "s1",
                    "file_len": 12,
                    "file_crc32": 1234,
                    "chunk_size": 6,
                    "chunk_count": 2,
                },
            }
        )[0].payload
        self.assertTrue(begin["ok"])
        self.assertEqual("OTA session started", begin["message"])
        self.assertTrue(begin["data"]["active"])
        self.assertEqual("s1", begin["data"]["session_id"])
        self.assertEqual(2, begin["data"]["chunk_count"])

    def test_invalid_json_payload_publishes_debug_and_error_response(self):
        protocol = RealBoardProtocol(BoardConfig.default())

        messages = protocol.handle_payload_text("{bad-json")

        self.assertEqual("debug", messages[0].channel)
        self.assertEqual("emqx-gateway.debug.v1", messages[0].payload["schema"])
        self.assertEqual("invalid_json", messages[0].payload["event"])
        self.assertEqual("event", messages[1].channel)
        self.assertEqual(400, messages[1].payload["code"])
        self.assertEqual("invalid JSON", messages[1].payload["message"])


if __name__ == "__main__":
    unittest.main()
