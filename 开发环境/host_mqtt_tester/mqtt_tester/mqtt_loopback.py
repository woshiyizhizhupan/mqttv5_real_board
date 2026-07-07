from __future__ import annotations

import argparse
import json
import ssl
import sys
import threading
import time
import uuid
from dataclasses import dataclass
from typing import Any

import paho.mqtt.client as mqtt

from .mqtt_topics import build_default_topics


@dataclass
class LoopbackResult:
    ok: bool
    received: list[str]
    missing: list[str]
    elapsed_seconds: float
    broker: str
    port: int
    mqtt_version: str


def run_loopback(args: argparse.Namespace) -> LoopbackResult:
    topics = build_default_topics(args.product_key, args.device_name)
    expected_topics = topics.as_list()
    test_id = uuid.uuid4().hex[:12]
    received: set[str] = set()
    connected = threading.Event()
    completed = threading.Event()
    errors: list[str] = []

    client = _new_client(args.client_id or f"pc-loopback-{test_id}")

    if args.username:
        client.username_pw_set(args.username, args.password or None)

    if args.tls:
        client.tls_set(
            ca_certs=args.ca,
            certfile=args.cert,
            keyfile=args.key,
            tls_version=ssl.PROTOCOL_TLS_CLIENT,
        )
        client.tls_insecure_set(args.insecure)

    def on_connect(client: mqtt.Client, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        if not is_success_reason_code(reason_code):
            errors.append(f"connect failed: {reason_code}")
            connected.set()
            return
        for topic in expected_topics:
            client.subscribe(topic, qos=0)
        connected.set()

    def on_message(client: mqtt.Client, userdata: Any, message: mqtt.MQTTMessage) -> None:
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except Exception:
            return
        if payload.get("test_id") != test_id:
            return
        received.add(message.topic)
        if received == set(expected_topics):
            completed.set()

    client.on_connect = on_connect
    client.on_message = on_message

    start = time.monotonic()
    client.loop_start()
    try:
        _connect(client, args.host, args.port)
        if not connected.wait(args.timeout):
            errors.append("connect timeout")
        if not errors:
            time.sleep(0.3)
            for name, topic in topics.as_named_pairs():
                payload = {
                    "test_id": test_id,
                    "source": "pc_host_tester",
                    "name": name,
                    "timestamp": int(time.time()),
                }
                info = client.publish(topic, json.dumps(payload, ensure_ascii=False), qos=0)
                info.wait_for_publish(timeout=args.timeout)
            completed.wait(args.timeout)
    finally:
        client.disconnect()
        client.loop_stop()

    missing = [topic for topic in expected_topics if topic not in received]
    if errors and not missing:
        missing = expected_topics
    return LoopbackResult(
        ok=not errors and not missing,
        received=[topic for topic in expected_topics if topic in received],
        missing=missing,
        elapsed_seconds=round(time.monotonic() - start, 3),
        broker=args.host,
        port=args.port,
        mqtt_version="5.0",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="MQTT 5.0 loopback tester for local EMQX.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--product-key", default="local_pk")
    parser.add_argument("--device-name", default="hc32f460_dev")
    parser.add_argument("--client-id")
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--ca")
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--insecure", action="store_true")
    parser.add_argument("--json", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    result = run_loopback(args)
    if args.json:
        print(json.dumps(result.__dict__, ensure_ascii=False, indent=2))
    else:
        status = "PASS" if result.ok else "FAIL"
        print(f"{status} MQTT {result.mqtt_version} loopback {result.broker}:{result.port}")
        print(f"received={len(result.received)} missing={len(result.missing)} elapsed={result.elapsed_seconds}s")
        for topic in result.received:
            print(f"  OK   {topic}")
        for topic in result.missing:
            print(f"  MISS {topic}")
    return 0 if result.ok else 1


def _new_client(client_id: str) -> mqtt.Client:
    try:
        return mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id,
            protocol=mqtt.MQTTv5,
        )
    except TypeError:
        return mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv5)


def is_success_reason_code(reason_code: Any) -> bool:
    if hasattr(reason_code, "is_failure"):
        is_failure = reason_code.is_failure
        return not is_failure() if callable(is_failure) else not is_failure
    if hasattr(reason_code, "value"):
        return reason_code.value == 0
    return int(reason_code) == 0


def _connect(client: mqtt.Client, host: str, port: int) -> None:
    try:
        client.connect(host, port, keepalive=60, clean_start=mqtt.MQTT_CLEAN_START_FIRST_ONLY)
    except TypeError:
        client.connect(host, port, keepalive=60)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
