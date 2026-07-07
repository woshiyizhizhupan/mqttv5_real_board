from __future__ import annotations

import argparse
import json
import ssl
import sys
import threading
import time
import uuid
from pathlib import Path
from typing import Any

from .protocol import BoardConfig, ProtocolPublish, RealBoardProtocol
from .topics import CityDeviceTopics


def topic_for_channel(topics: CityDeviceTopics, channel: str) -> str:
    if channel == "telemetry":
        return topics.telemetry_up
    if channel == "event":
        return topics.event_up
    if channel == "debug":
        return topics.debug_up
    raise ValueError(f"unknown publish channel: {channel}")


class JsonlLogger:
    def __init__(self, log_dir: Path | None):
        self.path: Path | None = None
        if log_dir is not None:
            log_dir.mkdir(parents=True, exist_ok=True)
            self.path = log_dir / f"real_board_emulator_{time.strftime('%Y%m%d-%H%M%S')}.jsonl"

    def write(self, record: dict[str, Any]) -> None:
        text = json.dumps(record, ensure_ascii=False, sort_keys=True)
        print(text)
        if self.path is not None:
            with self.path.open("a", encoding="utf-8") as stream:
                stream.write(text + "\n")


class RealBoardMqttEmulator:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.config = BoardConfig.default()
        self.config.broker_host = args.host
        self.config.broker_port = args.port
        self.config.device_id = args.device_id
        self.config.device_name = args.device_name or args.device_id
        self.config.city_id = args.city_id
        self.config.pole_id = args.pole_id
        self.config.username = args.username or ""
        self.config.password = args.password or ""
        self.config.qos = [args.qos] * 5
        self.config.topics = self.config.default_topics().as_list()
        self.protocol = RealBoardProtocol(self.config)
        self.topics = self.config.city_topics()
        self.logger = JsonlLogger(args.log_dir)
        self.connected = threading.Event()
        self.stop_requested = threading.Event()
        self.errors: list[str] = []
        self.client = self._new_mqtt_client(args.client_id or args.device_id)

    def run(self) -> int:
        self._configure_client()
        self.client.loop_start()
        start = time.monotonic()
        next_telemetry = start
        try:
            self._connect()
            if not self.connected.wait(10.0):
                self.errors.append("MQTT connect timeout")
            if self.errors:
                for error in self.errors:
                    self.logger.write({"direction": "error", "message": error})
                return 1

            max_runtime = self.args.max_runtime
            while not self.stop_requested.is_set():
                now = time.monotonic()
                if max_runtime is not None and (now - start) >= max_runtime:
                    break
                if now >= next_telemetry:
                    self.protocol.advance_simulation(100)
                    self._publish("telemetry", self.protocol.build_telemetry_payload())
                    event = self.protocol.pop_queued_event_payload()
                    if event is not None:
                        self._publish("event", event)
                    next_telemetry = now + self.args.telemetry_interval
                time.sleep(0.05)
            return 0
        finally:
            self.client.disconnect()
            self.client.loop_stop()

    def _configure_client(self) -> None:
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        if self.args.username:
            self.client.username_pw_set(self.args.username, self.args.password or None)
        if self.args.tls:
            self.client.tls_set(
                ca_certs=self.args.ca,
                certfile=self.args.cert,
                keyfile=self.args.key,
                tls_version=ssl.PROTOCOL_TLS_CLIENT,
            )
            self.client.tls_insecure_set(self.args.insecure)

    def _connect(self) -> None:
        try:
            self.client.connect(self.args.host, self.args.port, keepalive=60, clean_start=self._clean_start_value())
        except TypeError:
            self.client.connect(self.args.host, self.args.port, keepalive=60)

    def _on_connect(self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        if not is_success_reason_code(reason_code):
            self.errors.append(f"MQTT connect failed: {reason_code}")
            self.connected.set()
            return
        for topic in self.topics.downlink_topics():
            client.subscribe(topic, qos=self.args.qos)
            self.logger.write({"direction": "subscribe", "topic": topic, "qos": self.args.qos})
        self.connected.set()
        self._publish("event", self.protocol.build_status_payload("online", "MQTT v5 connected"))

    def _on_message(self, client: Any, userdata: Any, message: Any) -> None:
        payload_text = message.payload.decode("utf-8", errors="replace")
        self.logger.write({"direction": "downlink", "topic": message.topic, "payload": payload_text})
        for outbound in self.protocol.handle_payload_text(payload_text):
            self._publish_outbound(outbound)
        event = self.protocol.pop_queued_event_payload()
        if event is not None:
            self._publish("event", event)

    def _publish_outbound(self, outbound: ProtocolPublish) -> None:
        self._publish(outbound.channel, outbound.payload)

    def _publish(self, channel: str, payload: dict[str, Any]) -> None:
        topic = topic_for_channel(self.topics, channel)
        payload_text = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        self.client.publish(topic, payload_text, qos=self.args.qos)
        self.logger.write({"direction": "uplink", "channel": channel, "topic": topic, "payload": payload})

    def _new_mqtt_client(self, client_id: str) -> Any:
        mqtt = _load_paho()
        try:
            return mqtt.Client(
                callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
                client_id=client_id,
                protocol=mqtt.MQTTv5,
            )
        except TypeError:
            return mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv5)

    def _clean_start_value(self) -> Any:
        mqtt = _load_paho()
        return getattr(mqtt, "MQTT_CLEAN_START_FIRST_ONLY", True)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="PC-side MQTT 5.0 emulator for mqttv5_real_board.", allow_abbrev=False)
    parser.add_argument("--host", default="39.103.154.108")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--qos", type=int, default=2, choices=(0, 1, 2))
    parser.add_argument("--client-id")
    parser.add_argument("--device-id", default="GM400-452089")
    parser.add_argument("--city-id", default="tjw")
    parser.add_argument("--pole-id", default="pole001")
    parser.add_argument("--device-name")
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--ca")
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--insecure", action="store_true")

    sub = parser.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run", help="Run the board emulator until stopped or max runtime elapses.")
    run.add_argument("--telemetry-interval", type=float, default=5.0)
    run.add_argument("--max-runtime", type=float)
    run.add_argument("--log-dir", type=Path)
    run.set_defaults(func=run_emulator)
    return parser


def run_emulator(args: argparse.Namespace) -> int:
    if args.telemetry_interval <= 0:
        raise ValueError("telemetry interval must be greater than zero")
    return RealBoardMqttEmulator(args).run()


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    return int(args.func(args))


def is_success_reason_code(reason_code: Any) -> bool:
    if hasattr(reason_code, "is_failure"):
        is_failure = reason_code.is_failure
        return not is_failure() if callable(is_failure) else not is_failure
    if hasattr(reason_code, "value"):
        return int(reason_code.value) == 0
    return int(reason_code) == 0


def _load_paho() -> Any:
    try:
        import paho.mqtt.client as mqtt
    except ModuleNotFoundError as exc:
        raise ModuleNotFoundError("paho-mqtt is not installed. Run: python -m pip install -r requirements.txt") from exc
    return mqtt


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
