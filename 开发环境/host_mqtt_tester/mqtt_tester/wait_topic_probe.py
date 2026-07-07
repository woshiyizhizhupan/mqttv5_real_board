from __future__ import annotations

import argparse
import sys
import threading
import uuid
from typing import Any

import paho.mqtt.client as mqtt

from .mqtt_loopback import is_success_reason_code


def run_probe(args: argparse.Namespace) -> int:
    connected = threading.Event()
    received = threading.Event()
    errors: list[str] = []
    payload_text = ""
    received_topic = ""
    client = _new_client(args.client_id or f"pc-wait-{uuid.uuid4().hex[:8]}")
    if args.username:
        client.username_pw_set(args.username, args.password or None)

    def on_connect(client: mqtt.Client, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        if not is_success_reason_code(reason_code):
            errors.append(f"connect failed: {reason_code}")
            connected.set()
            return
        client.subscribe(args.topic, qos=args.qos)
        connected.set()

    def on_message(client: mqtt.Client, userdata: Any, message: mqtt.MQTTMessage) -> None:
        nonlocal payload_text, received_topic
        received_topic = message.topic
        payload_text = message.payload.decode("utf-8", errors="replace")
        if args.expect and args.expect not in payload_text:
            return
        received.set()

    client.on_connect = on_connect
    client.on_message = on_message
    client.loop_start()
    try:
        _connect(client, args.host, args.port)
        if not connected.wait(args.timeout):
            errors.append("connect timeout")
        if not errors and not received.wait(args.timeout):
            errors.append("message timeout")
    finally:
        client.disconnect()
        client.loop_stop()

    if errors:
        for error in errors:
            print("ERROR=" + error)
        if payload_text:
            print("LAST_TOPIC=" + received_topic)
            print("LAST_PAYLOAD=" + payload_text)
        return 1
    print("WAIT_TOPIC_OK=1")
    print("TOPIC=" + received_topic)
    print("PAYLOAD=" + payload_text)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Wait for one MQTT message on a topic.")
    parser.add_argument("--host", default="192.168.0.110")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--topic", required=True)
    parser.add_argument("--qos", type=int, default=2, choices=(0, 1, 2))
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--expect")
    parser.add_argument("--client-id")
    parser.add_argument("--username")
    parser.add_argument("--password")
    return parser


def _new_client(client_id: str) -> mqtt.Client:
    try:
        return mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id,
            protocol=mqtt.MQTTv5,
        )
    except TypeError:
        return mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv5)


def _connect(client: mqtt.Client, host: str, port: int) -> None:
    try:
        client.connect(host, port, keepalive=60, clean_start=mqtt.MQTT_CLEAN_START_FIRST_ONLY)
    except TypeError:
        client.connect(host, port, keepalive=60)


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return run_probe(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
