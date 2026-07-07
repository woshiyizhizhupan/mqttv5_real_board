import sys
import unittest
from pathlib import Path
from unittest.mock import patch


PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT))

from real_board_emulator.mqtt_app import RealBoardMqttEmulator, build_arg_parser, topic_for_channel
from real_board_emulator.protocol import BoardConfig


class MqttAppTests(unittest.TestCase):
    def test_channel_to_topic_maps_real_board_outputs(self):
        topics = BoardConfig.default().city_topics()

        self.assertEqual(topics.telemetry_up, topic_for_channel(topics, "telemetry"))
        self.assertEqual(topics.event_up, topic_for_channel(topics, "event"))
        self.assertEqual(topics.debug_up, topic_for_channel(topics, "debug"))

    def test_cli_defaults_target_remote_emqx_and_current_device(self):
        args = build_arg_parser().parse_args(["run", "--max-runtime", "1"])

        self.assertEqual("39.103.154.108", args.host)
        self.assertEqual(1883, args.port)
        self.assertEqual("GM400-452089", args.device_id)
        self.assertEqual("tjw", args.city_id)
        self.assertEqual("pole001", args.pole_id)
        self.assertEqual(2, args.qos)
        self.assertEqual(5.0, args.telemetry_interval)

    def test_publish_does_not_block_waiting_for_qos_ack(self):
        class PublishInfo:
            wait_called = False

            def wait_for_publish(self, timeout=None):
                self.wait_called = True
                raise AssertionError("publish must not block in callback-safe path")

        class FakeClient:
            def __init__(self):
                self.info = PublishInfo()
                self.published = []

            def publish(self, topic, payload, qos=0):
                self.published.append((topic, payload, qos))
                return self.info

        args = build_arg_parser().parse_args(["run", "--max-runtime", "1"])
        fake = FakeClient()
        with patch.object(RealBoardMqttEmulator, "_new_mqtt_client", return_value=fake):
            emulator = RealBoardMqttEmulator(args)
        emulator.logger = type("SilentLogger", (), {"write": lambda self, record: None})()

        emulator._publish("event", {"schema": "test"})

        self.assertFalse(fake.info.wait_called)
        self.assertEqual("city/tjw/pole/pole001/device/GM400-452089/event", fake.published[0][0])
        self.assertEqual(2, fake.published[0][2])


if __name__ == "__main__":
    unittest.main()
