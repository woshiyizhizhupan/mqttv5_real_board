from __future__ import annotations

import argparse
import base64
import json
import math
import ssl
import sys
import threading
import time
import uuid
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import paho.mqtt.client as mqtt

from .mqtt_loopback import is_success_reason_code


@dataclass(frozen=True)
class CityDeviceTopics:
    telemetry_up: str
    cmd_down: str
    event_up: str
    ota_down: str
    debug_up: str

    def subscribe_topics(self) -> list[str]:
        return [self.telemetry_up, self.event_up, self.debug_up]


@dataclass(frozen=True)
class LegacyUpgradeResponseFrame:
    frame_type: int
    cmd_type: int
    payload: bytes


def build_city_device_topics(city_id: str, pole_id: str, device_name: str, device_id: str | None = None) -> CityDeviceTopics:
    ota_device = device_id or device_name
    base = f"city/{city_id}/pole/{pole_id}/device/{device_name}"
    return CityDeviceTopics(
        telemetry_up=base + "/",
        cmd_down=base + "/get",
        event_up=base + "/event",
        ota_down=f"city/{city_id}/pole/{pole_id}/device/{ota_device}/ota",
        debug_up=base + "/debug",
    )


def crc32_bytes(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _write_u16_be(value: int) -> bytes:
    return int(value).to_bytes(2, "big")


def _write_u32_be(value: int) -> bytes:
    return int(value).to_bytes(4, "big")


def build_v2_legacy_ota_frame(cmd_type: int, payload: bytes) -> bytes:
    """Build V2 A5/0x01 wrapper around a B0-B6 legacy upgrade sub-frame."""
    if not 0 <= cmd_type <= 0xFF:
        raise ValueError("cmd_type must fit in one byte")
    if len(payload) > 0xFFFF:
        raise ValueError("payload is too large")

    inner_body = bytes([cmd_type]) + _write_u16_be(len(payload)) + payload
    inner = bytes([0xFE]) + inner_body + _write_u32_be(crc32_bytes(inner_body))
    outer_body = bytes([0xA5, 0x01]) + _write_u16_be(len(inner)) + inner
    return bytes([0xFE]) + outer_body + _write_u32_be(crc32_bytes(outer_body))


def build_legacy_frame_payload(frame: bytes, request_id: str | None = None) -> dict[str, Any]:
    return {
        "id": request_id or f"legacy-{uuid.uuid4().hex[:8]}",
        "cmd": "legacy_frame",
        "frame": base64.b64encode(frame).decode("ascii"),
    }


def build_legacy_b1_file_info_payload(image: bytes, chunk_size: int) -> bytes:
    if chunk_size <= 0:
        raise ValueError("chunk_size must be greater than zero")
    chunk_count = math.ceil(len(image) / chunk_size)
    return (
        _write_u32_be(len(image))
        + _write_u16_be(chunk_size)
        + _write_u16_be(chunk_count)
        + _write_u32_be(crc32_bytes(image))
    )


def build_legacy_b2_chunk_payload(index: int, chunk: bytes) -> bytes:
    if not 0 <= index <= 0xFFFF:
        raise ValueError("legacy chunk index must fit in two bytes")
    return _write_u16_be(index) + chunk


def decode_legacy_upgrade_response_frame(encoded_frame: str) -> LegacyUpgradeResponseFrame:
    raw = base64.b64decode(encoded_frame, validate=True)
    if len(raw) < 9:
        raise ValueError("legacy response frame is too short")
    if raw[0] != 0xFE:
        raise ValueError("legacy response frame missing 0xFE preamble")
    payload_len = int.from_bytes(raw[3:5], "big")
    expected_len = 1 + 1 + 1 + 2 + payload_len + 4
    if len(raw) != expected_len:
        raise ValueError("legacy response frame length mismatch")
    expected_crc = int.from_bytes(raw[-4:], "big")
    actual_crc = crc32_bytes(raw[1:-4])
    if actual_crc != expected_crc:
        raise ValueError("legacy response frame CRC mismatch")
    return LegacyUpgradeResponseFrame(frame_type=raw[1], cmd_type=raw[2], payload=raw[5:-4])


def split_chunks(data: bytes, chunk_size: int) -> Iterable[bytes]:
    if chunk_size <= 0:
        raise ValueError("chunk_size must be greater than zero")
    for offset in range(0, len(data), chunk_size):
        yield data[offset : offset + chunk_size]


def _parse_subscribe_result(result: Any) -> tuple[int | None, int | None]:
    if isinstance(result, tuple):
        rc = int(result[0]) if len(result) >= 1 and result[0] is not None else None
        mid = int(result[1]) if len(result) >= 2 and result[1] is not None else None
        return rc, mid
    rc_value = getattr(result, "rc", None)
    mid_value = getattr(result, "mid", None)
    rc = int(rc_value) if rc_value is not None else None
    mid = int(mid_value) if mid_value is not None else None
    return rc, mid


def build_ota_begin_payload(session_id: str, image: bytes, chunk_size: int = 1024, request_id: str | None = None) -> dict[str, Any]:
    chunks = math.ceil(len(image) / chunk_size)
    return {
        "id": request_id or f"ota-begin-{uuid.uuid4().hex[:8]}",
        "cmd": "ota_begin",
        "params": {
            "session_id": session_id,
            "file_len": len(image),
            "file_crc32": crc32_bytes(image),
            "chunk_size": chunk_size,
            "chunk_count": chunks,
        },
    }


def build_ota_chunk_payload(
    session_id: str,
    index: int,
    offset: int,
    chunk: bytes,
    request_id: str | None = None,
) -> dict[str, Any]:
    return {
        "id": request_id or f"ota-chunk-{index:04d}-{uuid.uuid4().hex[:8]}",
        "cmd": "ota_chunk",
        "params": {
            "session_id": session_id,
            "index": index,
            "offset": offset,
            "chunk_crc32": crc32_bytes(chunk),
            "data": base64.b64encode(chunk).decode("ascii"),
        },
    }


def build_ota_end_payload(session_id: str, request_id: str | None = None) -> dict[str, Any]:
    return {
        "id": request_id or f"ota-end-{uuid.uuid4().hex[:8]}",
        "cmd": "ota_end",
        "params": {"session_id": session_id},
    }


def build_ota_status_payload(session_id: str, request_id: str | None = None) -> dict[str, Any]:
    return {
        "id": request_id or f"ota-status-{uuid.uuid4().hex[:8]}",
        "cmd": "ota_status",
        "params": {"session_id": session_id},
    }


def build_command_payload(cmd: str, params: dict[str, Any] | None = None, request_id: str | None = None) -> dict[str, Any]:
    payload: dict[str, Any] = {"id": request_id or f"cmd-{uuid.uuid4().hex[:8]}", "cmd": cmd}
    if params is not None:
        payload["params"] = params
    return payload


class JsonlRecorder:
    def __init__(self, output_dir: Path | None):
        self.output_dir = output_dir
        self.path: Path | None = None
        if output_dir is not None:
            output_dir.mkdir(parents=True, exist_ok=True)
            self.path = output_dir / f"sim_board_mqtt_{time.strftime('%Y%m%d-%H%M%S')}.jsonl"

    def write(self, record: dict[str, Any]) -> None:
        text = json.dumps(record, ensure_ascii=False, sort_keys=True)
        print(text)
        if self.path is not None:
            with self.path.open("a", encoding="utf-8") as f:
                f.write(text + "\n")


class SimBoardMqttClient:
    def __init__(
        self,
        host: str,
        port: int,
        client_id: str | None = None,
        qos: int = 2,
        username: str | None = None,
        password: str | None = None,
        tls: bool = False,
        ca: str | None = None,
        cert: str | None = None,
        key: str | None = None,
        insecure: bool = False,
    ):
        self.host = host
        self.port = port
        self.qos = qos
        self.connected = threading.Event()
        self.errors: list[str] = []
        self.responses: dict[str, dict[str, Any]] = {}
        self.response_events: dict[str, threading.Event] = {}
        self.subscription_events: dict[int, threading.Event] = {}
        self.subscription_topics: dict[int, str] = {}
        self.subscribed_topics: set[str] = set()
        self.subscription_lock = threading.Lock()
        self.messages: list[dict[str, Any]] = []
        self.client = _new_client(client_id or f"sim-server-{uuid.uuid4().hex[:8]}")
        if username:
            self.client.username_pw_set(username, password or None)
        if tls:
            self.client.tls_set(ca_certs=ca, certfile=cert, keyfile=key, tls_version=ssl.PROTOCOL_TLS_CLIENT)
            self.client.tls_insecure_set(insecure)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_subscribe = self._on_subscribe

    def connect(self, subscribe_topics: Iterable[str] = ()) -> None:
        self._subscribe_topics = list(subscribe_topics)
        self.client.loop_start()
        _connect(self.client, self.host, self.port)
        if not self.connected.wait(8.0):
            raise TimeoutError("MQTT connect timeout")
        if self.errors:
            raise RuntimeError("; ".join(self.errors))
        for topic in self._subscribe_topics:
            self._subscribe_and_wait(topic, 8.0)

    def disconnect(self) -> None:
        self.client.disconnect()
        self.client.loop_stop()

    def publish_json(self, topic: str, payload: dict[str, Any]) -> None:
        info = self.client.publish(topic, json.dumps(payload, ensure_ascii=False), qos=self.qos)
        info.wait_for_publish(timeout=8.0)

    def request(self, topic: str, response_topic: str, payload: dict[str, Any], timeout: float = 12.0) -> dict[str, Any]:
        request_id = str(payload.get("id") or f"req-{uuid.uuid4().hex[:8]}")
        payload["id"] = request_id
        event = threading.Event()
        self.response_events[request_id] = event
        self._subscribe_and_wait(response_topic, timeout)
        self.publish_json(topic, payload)
        if not event.wait(timeout):
            raise TimeoutError(f"response timeout for {request_id}")
        return self.responses[request_id]

    def _on_connect(self, client: mqtt.Client, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        if not is_success_reason_code(reason_code):
            self.errors.append(f"connect failed: {reason_code}")
            self.connected.set()
            return
        self.connected.set()

    def _subscribe_and_wait(self, topic: str, timeout: float) -> None:
        with self.subscription_lock:
            if topic in self.subscribed_topics:
                return

        result = self.client.subscribe(topic, qos=self.qos)
        rc, mid = _parse_subscribe_result(result)
        if rc not in (None, 0):
            raise RuntimeError(f"subscribe failed for {topic}: rc={rc}")
        if mid is None:
            time.sleep(0.3)
            with self.subscription_lock:
                self.subscribed_topics.add(topic)
            return

        event = threading.Event()
        with self.subscription_lock:
            self.subscription_events[mid] = event
            self.subscription_topics[mid] = topic

        if not event.wait(timeout):
            with self.subscription_lock:
                self.subscription_events.pop(mid, None)
                self.subscription_topics.pop(mid, None)
            raise TimeoutError(f"subscribe timeout for {topic}")

    def _on_subscribe(self, client: mqtt.Client, userdata: Any, mid: int, reason_codes: Any = None, properties: Any = None) -> None:
        with self.subscription_lock:
            event = self.subscription_events.pop(mid, None)
            topic = self.subscription_topics.pop(mid, None)
            if topic is not None:
                self.subscribed_topics.add(topic)
        if event is not None:
            event.set()

    def _on_message(self, client: mqtt.Client, userdata: Any, message: mqtt.MQTTMessage) -> None:
        payload_text = message.payload.decode("utf-8", errors="replace")
        record: dict[str, Any] = {
            "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "topic": message.topic,
            "payload": payload_text,
        }
        try:
            payload = json.loads(payload_text)
            record["json"] = payload
            request_id = payload.get("id")
            if isinstance(request_id, str) and request_id in self.response_events:
                self.responses[request_id] = payload
                self.response_events[request_id].set()
        except Exception:
            pass
        self.messages.append(record)


def run_watch(args: argparse.Namespace) -> int:
    topics = _topics_from_args(args)
    recorder = JsonlRecorder(args.log_dir)
    client = _client_from_args(args)
    client.connect(topics.subscribe_topics())
    deadline = time.monotonic() + args.timeout
    try:
        seen = 0
        while time.monotonic() < deadline:
            while seen < len(client.messages):
                recorder.write(client.messages[seen])
                seen += 1
            time.sleep(0.1)
    finally:
        client.disconnect()
    return 0


def run_send_command(args: argparse.Namespace) -> int:
    client = _client_from_args(args)
    client.connect()
    try:
        params = json.loads(args.params_json) if args.params_json else None
        response = client.request(args.down_topic, args.response_topic, build_command_payload(args.cmd, params), args.timeout)
        print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
        return 0 if response.get("ok") is True else 1
    finally:
        client.disconnect()


def run_ota_send(args: argparse.Namespace) -> int:
    image = Path(args.image).read_bytes()
    session_id = args.session_id or f"ota-{time.strftime('%Y%m%d-%H%M%S')}"
    recorder = JsonlRecorder(args.log_dir)
    client = _client_from_args(args)
    client.connect()
    try:
        begin = build_ota_begin_payload(session_id, image, args.chunk_size)
        recorder.write({"direction": "publish", "phase": "begin", "topic": args.ota_topic, "payload": begin})
        response = client.request(args.ota_topic, args.response_topic, begin, args.timeout)
        recorder.write({"direction": "response", "phase": "begin", "topic": args.response_topic, "payload": response})
        if response.get("ok") is not True:
            return 1

        chunks = list(split_chunks(image, args.chunk_size))
        offset = 0
        for index, chunk in enumerate(chunks):
            payload = build_ota_chunk_payload(session_id, index, offset, chunk)
            recorder.write({"direction": "publish", "phase": "chunk", "index": index, "topic": args.ota_topic, "payload": payload})
            response = _request_with_retries(client, args.ota_topic, args.response_topic, payload, args.timeout, args.retries)
            recorder.write({"direction": "response", "phase": "chunk", "index": index, "topic": args.response_topic, "payload": response})
            if response.get("ok") is not True:
                return 1
            offset += len(chunk)
            if index + 1 < len(chunks) and args.request_interval > 0:
                time.sleep(args.request_interval)

        end = build_ota_end_payload(session_id)
        recorder.write({"direction": "publish", "phase": "end", "topic": args.ota_topic, "payload": end})
        response = client.request(args.ota_topic, args.response_topic, end, args.timeout)
        recorder.write({"direction": "response", "phase": "end", "topic": args.response_topic, "payload": response})
        return 0 if response.get("ok") is True else 1
    finally:
        client.disconnect()


def _legacy_request(
    client: SimBoardMqttClient,
    ota_topic: str,
    response_topic: str,
    cmd_type: int,
    payload: bytes,
    timeout: float,
    phase: str,
    recorder: JsonlRecorder,
) -> tuple[dict[str, Any], LegacyUpgradeResponseFrame]:
    frame = build_v2_legacy_ota_frame(cmd_type, payload)
    request_payload = build_legacy_frame_payload(frame, request_id=f"legacy-{phase}-{uuid.uuid4().hex[:8]}")
    recorder.write(
        {
            "direction": "publish",
            "protocol": "v2-a5-legacy-ota",
            "phase": phase,
            "cmd_type": f"0x{cmd_type:02X}",
            "topic": ota_topic,
            "frame_hex": frame.hex(" ").upper(),
            "payload": request_payload,
        }
    )
    response = client.request(ota_topic, response_topic, request_payload, timeout)
    encoded_response = ""
    data = response.get("data")
    if isinstance(data, dict):
        encoded_response = str(data.get("frame") or "")
    decoded = decode_legacy_upgrade_response_frame(encoded_response)
    recorder.write(
        {
            "direction": "response",
            "protocol": "legacy-upgrade-response",
            "phase": phase,
            "topic": response_topic,
            "ok": response.get("ok"),
            "code": response.get("code"),
            "message": response.get("message"),
            "response_cmd_type": f"0x{decoded.cmd_type:02X}",
            "response_payload_hex": decoded.payload.hex(" ").upper(),
            "payload": response,
        }
    )
    return response, decoded


def _legacy_request_with_retries(
    client: SimBoardMqttClient,
    ota_topic: str,
    response_topic: str,
    cmd_type: int,
    payload: bytes,
    timeout: float,
    phase: str,
    recorder: JsonlRecorder,
    retries: int,
) -> tuple[dict[str, Any], LegacyUpgradeResponseFrame]:
    last_error: Exception | None = None
    for attempt in range(retries + 1):
        attempt_phase = phase if attempt == 0 else f"{phase}_retry_{attempt}"
        try:
            return _legacy_request(client, ota_topic, response_topic, cmd_type, payload, timeout, attempt_phase, recorder)
        except TimeoutError as exc:
            last_error = exc
            recorder.write(
                {
                    "direction": "retry",
                    "protocol": "v2-a5-legacy-ota",
                    "phase": attempt_phase,
                    "cmd_type": f"0x{cmd_type:02X}",
                    "attempt": attempt + 1,
                    "max_attempts": retries + 1,
                    "error": str(exc),
                }
            )
            if attempt >= retries:
                break
            time.sleep(0.5)
    assert last_error is not None
    raise last_error


def run_legacy_ota_send(args: argparse.Namespace) -> int:
    image = Path(args.image).read_bytes()
    recorder = JsonlRecorder(args.log_dir)
    client = _client_from_args(args)
    chunks = list(split_chunks(image, args.chunk_size))
    client.connect()
    try:
        response, decoded = _legacy_request(client, args.ota_topic, args.response_topic, 0xB0, b"", args.timeout, "b0_start", recorder)
        if response.get("ok") is not True or decoded.cmd_type != 0xB0:
            return 1

        file_info = build_legacy_b1_file_info_payload(image, args.chunk_size)
        response, decoded = _legacy_request(client, args.ota_topic, args.response_topic, 0xB1, file_info, args.timeout, "b1_file_info", recorder)
        if response.get("ok") is not True or decoded.payload != b"\x01":
            return 1

        for index, chunk in enumerate(chunks):
            chunk_payload = build_legacy_b2_chunk_payload(index, chunk)
            response, decoded = _legacy_request_with_retries(
                client,
                args.ota_topic,
                args.response_topic,
                0xB2,
                chunk_payload,
                args.timeout,
                f"b2_chunk_{index:04d}",
                recorder,
                args.retries,
            )
            if response.get("ok") is not True or len(decoded.payload) < 2 or decoded.payload[0] != 1:
                return 1
            if index + 1 < len(chunks) and args.request_interval > 0:
                time.sleep(args.request_interval)

        for retry in range(args.check_retries + 1):
            response, decoded = _legacy_request(client, args.ota_topic, args.response_topic, 0xB3, b"", args.timeout, f"b3_check_{retry}", recorder)
            if len(decoded.payload) >= 3 and decoded.payload[0:2] == b"\xFF\xFF" and decoded.payload[2] == 1:
                break
            if retry >= args.check_retries:
                return 1
            if len(decoded.payload) >= 2:
                lost_index = int.from_bytes(decoded.payload[0:2], "big")
                if 0 <= lost_index < len(chunks):
                    chunk_payload = build_legacy_b2_chunk_payload(lost_index, chunks[lost_index])
                    response, decoded = _legacy_request_with_retries(
                        client,
                        args.ota_topic,
                        args.response_topic,
                        0xB2,
                        chunk_payload,
                        args.timeout,
                        f"b2_retransmit_{lost_index:04d}",
                        recorder,
                        args.retries,
                    )
                    if response.get("ok") is not True or len(decoded.payload) < 2 or decoded.payload[0] != 1:
                        return 1
            if args.request_interval > 0:
                time.sleep(args.request_interval)

        response, decoded = _legacy_request(client, args.ota_topic, args.response_topic, 0xB4, b"", args.timeout, "b4_end", recorder)
        if response.get("ok") is not True or decoded.payload != b"\x01":
            return 1

        recorder.write(
            {
                "direction": "summary",
                "protocol": "v2-a5-legacy-ota",
                "image": str(Path(args.image)),
                "image_bytes": len(image),
                "image_crc32": f"0x{crc32_bytes(image):08X}",
                "chunk_size": args.chunk_size,
                "chunk_count": len(chunks),
                "result": "ota_reboot_scheduled",
            }
        )
        return 0
    finally:
        client.disconnect()


def _request_with_retries(
    client: SimBoardMqttClient,
    topic: str,
    response_topic: str,
    payload: dict[str, Any],
    timeout: float,
    retries: int,
) -> dict[str, Any]:
    last_error: Exception | None = None
    for attempt in range(retries + 1):
        try:
            return client.request(topic, response_topic, payload, timeout)
        except TimeoutError as exc:
            last_error = exc
            if attempt >= retries:
                break
            time.sleep(0.5)
    assert last_error is not None
    raise last_error


def run_smoke(args: argparse.Namespace) -> int:
    topics = _topics_from_args(args)
    client = _client_from_args(args)
    client.connect(topics.subscribe_topics())
    try:
        payloads = (
            build_command_payload("ping"),
            build_command_payload("get_status"),
            build_command_payload("sim_set", {"lamp1_brightness": 75, "relay1_on": True, "rs485_online": True}),
            build_command_payload("sim_event", {"event": "manual_test", "level": "info"}),
            build_ota_status_payload(args.session_id or "smoke"),
        )
        for index, payload in enumerate(payloads):
            response = client.request(topics.cmd_down if not payload["cmd"].startswith("ota_") else topics.ota_down, topics.event_up, payload, args.timeout)
            print(json.dumps(response, ensure_ascii=False, sort_keys=True))
            if response.get("ok") is not True:
                return 1
            if index + 1 < len(payloads) and args.request_interval > 0:
                time.sleep(args.request_interval)
        return 0
    finally:
        client.disconnect()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Simple EMQX test server/client for the simulated HC32F460 board.", allow_abbrev=False)
    parser.add_argument("--host", default="192.168.0.110")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--qos", type=int, default=2, choices=(0, 1, 2))
    parser.add_argument("--client-id")
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--ca")
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--insecure", action="store_true")

    topic_group = parser.add_argument_group("city topic defaults")
    topic_group.add_argument("--city-id", default="tjw")
    topic_group.add_argument("--pole-id", default="pole001")
    topic_group.add_argument("--device-name", default="GM400-452089")
    topic_group.add_argument("--device-id")

    sub = parser.add_subparsers(dest="command", required=True)

    watch = sub.add_parser("watch", help="Subscribe to telemetry/event/debug topics and print JSONL records.")
    watch.add_argument("--timeout", type=float, default=60.0)
    watch.add_argument("--log-dir", type=Path)
    watch.set_defaults(func=run_watch)

    send = sub.add_parser("send-command", help="Send one command and wait for the response.")
    send.add_argument("--down-topic", required=True)
    send.add_argument("--response-topic", required=True)
    send.add_argument("--cmd", required=True)
    send.add_argument("--params-json")
    send.add_argument("--timeout", type=float, default=12.0)
    send.set_defaults(func=run_send_command)

    ota = sub.add_parser("ota-send", help="Send a local .bin image to APP2 through EMQX.")
    ota.add_argument("--ota-topic", required=True)
    ota.add_argument("--response-topic", required=True)
    ota.add_argument("--image", required=True)
    ota.add_argument("--session-id")
    ota.add_argument("--chunk-size", type=int, default=512)
    ota.add_argument("--timeout", type=float, default=15.0)
    ota.add_argument("--request-interval", type=float, default=0.0)
    ota.add_argument("--retries", type=int, default=0)
    ota.add_argument("--log-dir", type=Path)
    ota.set_defaults(func=run_ota_send)

    legacy_ota = sub.add_parser("legacy-ota-send", help="Send a local .bin image through V2 A5/0x01 nested legacy B0-B6 OTA frames.")
    legacy_ota.add_argument("--ota-topic", required=True)
    legacy_ota.add_argument("--response-topic", required=True)
    legacy_ota.add_argument("--image", required=True)
    legacy_ota.add_argument("--chunk-size", type=int, default=512)
    legacy_ota.add_argument("--timeout", type=float, default=15.0)
    legacy_ota.add_argument("--request-interval", type=float, default=0.0)
    legacy_ota.add_argument("--retries", type=int, default=2)
    legacy_ota.add_argument("--check-retries", type=int, default=3)
    legacy_ota.add_argument("--log-dir", type=Path)
    legacy_ota.set_defaults(func=run_legacy_ota_send)

    smoke = sub.add_parser("smoke", help="Run ping/status/sim/ota-status commands using city topic defaults.")
    smoke.add_argument("--session-id")
    smoke.add_argument("--timeout", type=float, default=12.0)
    smoke.add_argument("--request-interval", type=float, default=1.5)
    smoke.set_defaults(func=run_smoke)

    return parser


def _topics_from_args(args: argparse.Namespace) -> CityDeviceTopics:
    return build_city_device_topics(args.city_id, args.pole_id, args.device_name, args.device_id)


def _client_from_args(args: argparse.Namespace) -> SimBoardMqttClient:
    tls_kwargs: dict[str, Any] = {}
    if getattr(args, "tls", False):
        tls_kwargs = {
            "tls": True,
            "ca": getattr(args, "ca", None),
            "cert": getattr(args, "cert", None),
            "key": getattr(args, "key", None),
            "insecure": getattr(args, "insecure", False),
        }
    return SimBoardMqttClient(
        args.host,
        args.port,
        args.client_id,
        args.qos,
        username=getattr(args, "username", None),
        password=getattr(args, "password", None),
        **tls_kwargs,
    )


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
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
