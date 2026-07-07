import base64
import argparse
import unittest
from unittest.mock import call, patch

from mqtt_tester.sim_board_server import (
    SimBoardMqttClient,
    build_legacy_b1_file_info_payload,
    build_legacy_b2_chunk_payload,
    build_legacy_frame_payload,
    build_v2_legacy_ota_frame,
    build_parser,
    build_ota_begin_payload,
    build_ota_chunk_payload,
    build_ota_end_payload,
    build_ota_status_payload,
    crc32_bytes,
    decode_legacy_upgrade_response_frame,
    run_smoke,
    split_chunks,
)


def _read_gui_source() -> str:
    from pathlib import Path

    return (Path(__file__).resolve().parents[1] / "mqtt_host_gui.py").read_text(encoding="utf-8")


class SimBoardServerTests(unittest.TestCase):
    def test_gui_defaults_match_public_real_board_contract(self):
        gui_source = _read_gui_source()

        self.assertIn('"39.103.154.108"', gui_source)
        self.assertIn('"GM400-452089"', gui_source)
        self.assertIn('"public"', gui_source)
        self.assertIn("build_city_device_topics", gui_source)
        self.assertIn("city_id", gui_source)
        self.assertIn("pole_id", gui_source)
        self.assertIn("cmd_down", gui_source)
        self.assertIn("ota_down", gui_source)
        self.assertNotIn("ProductKey", gui_source)

    def test_builds_full_ota_command_sequence(self):
        image = bytes(range(16))
        chunks = list(split_chunks(image, 6))

        self.assertEqual([b"\x00\x01\x02\x03\x04\x05", b"\x06\x07\x08\x09\x0a\x0b", b"\x0c\x0d\x0e\x0f"], chunks)

        begin = build_ota_begin_payload("ota-1", image, chunk_size=6, request_id="req-begin")
        self.assertEqual("ota_begin", begin["cmd"])
        self.assertEqual("req-begin", begin["id"])
        self.assertEqual("ota-1", begin["params"]["session_id"])
        self.assertEqual(16, begin["params"]["file_len"])
        self.assertEqual(3, begin["params"]["chunk_count"])
        self.assertEqual(6, begin["params"]["chunk_size"])
        self.assertEqual(crc32_bytes(image), begin["params"]["file_crc32"])

        chunk = build_ota_chunk_payload("ota-1", 1, 6, chunks[1], request_id="req-chunk")
        self.assertEqual("ota_chunk", chunk["cmd"])
        self.assertEqual("req-chunk", chunk["id"])
        self.assertEqual("ota-1", chunk["params"]["session_id"])
        self.assertEqual(1, chunk["params"]["index"])
        self.assertEqual(6, chunk["params"]["offset"])
        self.assertEqual(base64.b64encode(chunks[1]).decode("ascii"), chunk["params"]["data"])
        self.assertEqual(crc32_bytes(chunks[1]), chunk["params"]["chunk_crc32"])

        end = build_ota_end_payload("ota-1", request_id="req-end")
        self.assertEqual("ota_end", end["cmd"])
        self.assertEqual("ota-1", end["params"]["session_id"])

        status = build_ota_status_payload("ota-1", request_id="req-status")
        self.assertEqual("ota_status", status["cmd"])
        self.assertEqual("ota-1", status["params"]["session_id"])

    def test_builds_v2_legacy_a5_upgrade_frame_from_protocol_example(self):
        frame = build_v2_legacy_ota_frame(0xB0, b"")

        self.assertEqual(
            bytes.fromhex("FE A5 01 00 08 FE B0 00 00 3A 1F 27 02 97 65 1E A6"),
            frame,
        )

        payload = build_legacy_frame_payload(frame, request_id="legacy-b0")
        self.assertEqual("legacy_frame", payload["cmd"])
        self.assertEqual("legacy-b0", payload["id"])
        self.assertEqual(base64.b64encode(frame).decode("ascii"), payload["frame"])

    def test_builds_legacy_ota_payloads_and_decodes_ack_frame(self):
        image = bytes(range(10))

        file_info = build_legacy_b1_file_info_payload(image, chunk_size=4)
        self.assertEqual(
            len(image).to_bytes(4, "big")
            + (4).to_bytes(2, "big")
            + (3).to_bytes(2, "big")
            + crc32_bytes(image).to_bytes(4, "big"),
            file_info,
        )

        self.assertEqual(b"\x00\x02abcd", build_legacy_b2_chunk_payload(2, b"abcd"))

        raw_ack = bytes.fromhex("FE A4 B1 00 01 01")
        raw_ack += crc32_bytes(raw_ack[1:]).to_bytes(4, "big")
        decoded = decode_legacy_upgrade_response_frame(base64.b64encode(raw_ack).decode("ascii"))
        self.assertEqual(0xA4, decoded.frame_type)
        self.assertEqual(0xB1, decoded.cmd_type)
        self.assertEqual(b"\x01", decoded.payload)

    def test_rejects_invalid_chunk_size(self):
        with self.assertRaises(ValueError):
            list(split_chunks(b"abc", 0))

    def test_request_waits_for_response_subscription_before_publish(self):
        class PublishInfo:
            def wait_for_publish(self, timeout=None):
                return True

        class FakeMqttClient:
            def __init__(self):
                self.actions = []

            def subscribe(self, topic, qos=0):
                self.actions.append(("subscribe", topic, qos))
                return (0, 7)

            def publish(self, topic, payload, qos=0):
                self.actions.append(("publish", topic, payload, qos))
                return PublishInfo()

        client = SimBoardMqttClient("127.0.0.1", 1883, qos=2)
        fake = FakeMqttClient()
        client.client = fake

        with self.assertRaisesRegex(TimeoutError, "subscribe timeout"):
            client.request("down/topic", "event/topic", {"id": "req-1", "cmd": "ping"}, timeout=0.01)

        self.assertEqual([("subscribe", "event/topic", 2)], fake.actions)

    def test_client_configures_tls_identity_when_requested(self):
        class FakeMqttClient:
            def __init__(self):
                self.calls = []

            def tls_set(self, ca_certs=None, certfile=None, keyfile=None, tls_version=None):
                self.calls.append(("tls_set", ca_certs, certfile, keyfile, tls_version))

            def tls_insecure_set(self, value):
                self.calls.append(("tls_insecure_set", value))

        fake = FakeMqttClient()

        with patch("mqtt_tester.sim_board_server._new_client", return_value=fake):
            SimBoardMqttClient(
                "127.0.0.1",
                8884,
                qos=2,
                tls=True,
                ca="ca.pem",
                cert="client.pem",
                key="client.key",
                insecure=True,
            )

        self.assertEqual("tls_set", fake.calls[0][0])
        self.assertEqual(("ca.pem", "client.pem", "client.key"), fake.calls[0][1:4])
        self.assertEqual(("tls_insecure_set", True), fake.calls[1])

    def test_parser_accepts_mtls_ota_send_arguments(self):
        args = build_parser().parse_args(
            [
                "--host",
                "192.168.0.110",
                "--port",
                "8884",
                "--tls",
                "--ca",
                "ca.pem",
                "--cert",
                "client.pem",
                "--key",
                "client.key",
                "ota-send",
                "--ota-topic",
                "city/tjw/pole/pole001/device/GM400/ota",
                "--response-topic",
                "city/tjw/pole/pole001/device/GM400/event",
                "--image",
                "candidate.bin",
                "--log-dir",
                "logs",
            ]
        )

        self.assertTrue(args.tls)
        self.assertEqual(8884, args.port)
        self.assertEqual("ca.pem", args.ca)
        self.assertEqual("logs", str(args.log_dir))

    def test_parser_accepts_legacy_ota_retry_arguments(self):
        args = build_parser().parse_args(
            [
                "--host",
                "39.103.154.108",
                "--username",
                "GM400-67890",
                "--password",
                "public",
                "legacy-ota-send",
                "--ota-topic",
                "city/zhxm01/pole/zhxm002/device/67890/ota",
                "--response-topic",
                "city/zhxm01/pole/zhxm002/device/67890/event",
                "--image",
                "candidate.bin",
                "--chunk-size",
                "768",
                "--retries",
                "3",
            ]
        )

        self.assertEqual("legacy-ota-send", args.command)
        self.assertEqual(768, args.chunk_size)
        self.assertEqual(3, args.retries)

    def test_ota_defaults_use_actual_512_byte_field_packets(self):
        regular = build_parser().parse_args(
            [
                "ota-send",
                "--ota-topic",
                "city/tjw/pole/pole001/device/GM400/ota",
                "--response-topic",
                "city/tjw/pole/pole001/device/GM400/event",
                "--image",
                "candidate.bin",
            ]
        )
        legacy = build_parser().parse_args(
            [
                "legacy-ota-send",
                "--ota-topic",
                "city/tjw/pole/pole001/device/GM400/ota",
                "--response-topic",
                "city/tjw/pole/pole001/device/GM400/event",
                "--image",
                "candidate.bin",
            ]
        )

        self.assertEqual(512, regular.chunk_size)
        self.assertEqual(512, legacy.chunk_size)

    def test_smoke_default_interval_is_suitable_for_real_board(self):
        args = build_parser().parse_args(
            [
                "smoke",
            ]
        )

        self.assertEqual(1.5, args.request_interval)

    def test_smoke_paces_requests_between_commands(self):
        class FakeClient:
            def __init__(self, host, port, client_id=None, qos=2, username=None, password=None, **tls_kwargs):
                self.requests = []
                self.username = username
                self.password = password
                self.tls_kwargs = tls_kwargs

            def connect(self, subscribe_topics=()):
                self.subscribe_topics = list(subscribe_topics)

            def request(self, topic, response_topic, payload, timeout=12.0):
                self.requests.append((topic, response_topic, payload["cmd"]))
                return {"ok": True, "cmd": payload["cmd"]}

            def disconnect(self):
                self.disconnected = True

        args = argparse.Namespace(
            host="127.0.0.1",
            port=1883,
            client_id=None,
            qos=2,
            city_id="tjw",
            pole_id="pole001",
            device_name="GM400-452089",
            device_id=None,
            session_id=None,
            timeout=1.0,
            request_interval=0.5,
            username="GM400-452089",
            password="public",
            tls=False,
            ca=None,
            cert=None,
            key=None,
            insecure=False,
        )

        with patch("mqtt_tester.sim_board_server.SimBoardMqttClient", FakeClient), patch("mqtt_tester.sim_board_server.time.sleep") as sleep:
            self.assertEqual(0, run_smoke(args))

        self.assertEqual([call(0.5), call(0.5), call(0.5), call(0.5)], sleep.call_args_list)


if __name__ == "__main__":
    unittest.main()
