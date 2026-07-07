from __future__ import annotations

import argparse
import json
import sys
import threading
import time
import uuid
from typing import Any

import paho.mqtt.client as mqtt

from .mqtt_loopback import is_success_reason_code


def run_probe(args: argparse.Namespace) -> int:
    request_id = args.request_id or f"probe-{uuid.uuid4().hex[:10]}"
    connected = threading.Event()
    received = threading.Event()
    errors: list[str] = []
    response_payload: dict[str, Any] | None = None
    response_topic = ""

    client = _new_client(args.client_id or f"pc-command-{uuid.uuid4().hex[:8]}")
    if args.username:
        client.username_pw_set(args.username, args.password or None)

    def on_connect(client: mqtt.Client, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        if not is_success_reason_code(reason_code):
            errors.append(f"connect failed: {reason_code}")
            connected.set()
            return
        client.subscribe(args.response_topic, qos=args.qos)
        connected.set()

    def on_message(client: mqtt.Client, userdata: Any, message: mqtt.MQTTMessage) -> None:
        nonlocal response_payload, response_topic
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except Exception:
            return
        if payload.get("id") != request_id:
            return
        response_payload = payload
        response_topic = message.topic
        received.set()

    client.on_connect = on_connect
    client.on_message = on_message
    client.loop_start()
    try:
        _connect(client, args.host, args.port)
        if not connected.wait(args.timeout):
            errors.append("connect timeout")
        if not errors:
            payload = {"id": request_id, "cmd": args.cmd}
            info = client.publish(args.down_topic, json.dumps(payload, ensure_ascii=False), qos=args.qos)
            info.wait_for_publish(timeout=args.timeout)
            if not received.wait(args.timeout):
                errors.append("response timeout")
    finally:
        client.disconnect()
        client.loop_stop()

    if errors:
        for error in errors:
            print("ERROR=" + error)
        return 1
    if response_payload is None:
        print("ERROR=no response payload")
        return 1
    if args.expect_topic and args.expect_topic not in json.dumps(response_payload, ensure_ascii=False):
        print("ERROR=expected topic missing: " + args.expect_topic)
        print("RESPONSE_TOPIC=" + response_topic)
        print("RESPONSE_PAYLOAD=" + json.dumps(response_payload, ensure_ascii=False, sort_keys=True))
        return 1
    print("MQTT_COMMAND_OK=1")
    print("REQUEST_ID=" + request_id)
    print("DOWN_TOPIC=" + args.down_topic)
    print("RESPONSE_TOPIC=" + response_topic)
    print("RESPONSE_PAYLOAD=" + json.dumps(response_payload, ensure_ascii=False, sort_keys=True))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Publish a board MQTT command and wait for its response.")
    parser.add_argument("--host", default="192.168.0.110")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--down-topic", required=True)
    parser.add_argument("--response-topic", required=True)
    parser.add_argument("--cmd", default="get_config")
    parser.add_argument("--qos", type=int, default=2, choices=(0, 1, 2))
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--request-id")
    parser.add_argument("--client-id")
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--expect-topic")
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
