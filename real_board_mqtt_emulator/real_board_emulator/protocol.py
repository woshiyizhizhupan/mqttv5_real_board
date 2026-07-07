from __future__ import annotations

import base64
import binascii
import json
import math
import time
import zlib
from dataclasses import dataclass, field
from typing import Any

from .topics import CityDeviceTopics, build_city_device_topics


APP1_START = 0x00004000
APP2_START = 0x00042000
APP2_END = 0x00076000
UPGRADE_STATE_ADDR = 0x0007A000
OTA_MAX_CHUNKS = 128
OTA_MAX_CHUNK_SIZE = 1024


@dataclass
class BoardConfig:
    device_id: str = "GM400-452089"
    broker_host: str = "39.103.154.108"
    broker_port: int = 1883
    username: str = ""
    password: str = ""
    city_id: str = "tjw"
    pole_id: str = "pole001"
    device_name: str = "GM400-452089"
    ntp_server: str = "129.6.15.28"
    tls_mode: int = 0
    tls_verify_peer: bool = True
    qos: list[int] = field(default_factory=lambda: [2, 2, 2, 2, 2])
    topics: list[str] = field(default_factory=list)
    firmware_version: str = "real-board-app1"
    legacy_downlink_supported: bool = False

    @classmethod
    def default(cls) -> "BoardConfig":
        config = cls()
        config.topics = config.default_topics().as_list()
        return config

    def default_topics(self) -> "TopicList":
        topics = build_city_device_topics(self.city_id, self.pole_id, self.device_name, self.device_id)
        return TopicList(
            [
                topics.telemetry_up,
                topics.cmd_down,
                topics.event_up,
                topics.ota_down,
                topics.debug_up,
            ]
        )

    def city_topics(self) -> CityDeviceTopics:
        if len(self.topics) == 5:
            return CityDeviceTopics(
                telemetry_up=self.topics[0],
                cmd_down=self.topics[1],
                event_up=self.topics[2],
                ota_down=self.topics[3],
                debug_up=self.topics[4],
            )
        return build_city_device_topics(self.city_id, self.pole_id, self.device_name, self.device_id)


class TopicList(list[str]):
    def as_list(self) -> list[str]:
        return list(self)


@dataclass
class BoardState:
    uptime_ticks: int = 0
    address: int = 1
    keepalive_s: int = 300
    temperature_c_x10: int = 250
    error_code: int = 0
    error_renew: bool = False
    voltage_mv: int = 220000
    current_ma: int = 1100
    active_power_mw: int = 242000
    reactive_power_mvar: int = 30000
    power_factor_x1000: int = 980
    energy_one_wh: int = 0
    energy_total_wh: int = 0
    pulse_count: int = 0
    meter_hlw8112_valid: bool = True
    environment_temperature_c_x10: int = 268
    environment_humidity_rh_x10: int = 560
    environment_pm25_ugm3: int = 18
    environment_co2_ppm: int = 520
    environment_illuminance_lux: int = 3200
    environment_valid: bool = True
    rs485_online: bool = True
    rs485_device_count: int = 3
    rs485_last_response_ms: int = 22
    rs485_tx_count: int = 0
    rs485_rx_count: int = 0
    rs485_error_count: int = 0
    rs485_valid: bool = True
    lamp1_brightness: int = 70
    lamp2_brightness: int = 0
    relay1_on: bool = True
    relay2_on: bool = False
    open_lamp_seconds: int = 0
    bright_time_seconds: int = 0
    bright_time_total_seconds: int = 0
    rs485_read_period_s: int = 30
    peripheral_type_code: int = 0
    last_peripheral_cmd: int = 0
    test_load_power_w: int = 0
    device_type: int = 0x4005
    reg_device_flag: int = 0
    test_count: int = 0
    test_results: int = 0
    test_enable: bool = False
    legacy_product_key: str = ""
    legacy_device_name: str = ""
    pending_event: str = ""
    pending_event_level: str = ""
    last_event_tick: int = -1
    reboot_requested: bool = False


@dataclass
class OtaState:
    active: bool = False
    complete: bool = False
    session_id: str = ""
    file_len: int = 0
    file_crc32: int = 0
    chunk_size: int = 0
    chunk_count: int = 0
    received_bytes: int = 0
    last_error: str = ""
    chunks: dict[int, bytes] = field(default_factory=dict)


@dataclass(frozen=True)
class ProtocolPublish:
    channel: str
    payload: dict[str, Any]


class RealBoardProtocol:
    def __init__(self, config: BoardConfig | None = None):
        self.config = config or BoardConfig.default()
        if not self.config.topics:
            self.config.topics = self.config.default_topics().as_list()
        self.state = BoardState()
        self.ota = OtaState()
        self.ntp_synced = True

    def advance_simulation(self, ticks: int = 100) -> None:
        ticks = max(1, int(ticks))
        self.state.uptime_ticks += ticks
        phase = self.state.uptime_ticks / 37.0
        load = max(0, self.state.lamp1_brightness if self.state.relay1_on else 0)
        self.state.voltage_mv = int(220000 + 850 * math.sin(phase))
        self.state.current_ma = int(260 + load * 16 + 90 * math.sin(phase / 2.0))
        self.state.active_power_mw = int(self.state.voltage_mv * self.state.current_ma / 1000)
        self.state.reactive_power_mvar = int(self.state.active_power_mw / 8)
        self.state.power_factor_x1000 = int(940 + 40 * (0.5 + 0.5 * math.sin(phase / 3.0)))
        self.state.pulse_count += ticks
        self.state.energy_one_wh += max(1, self.state.active_power_mw // 60000)
        self.state.energy_total_wh += max(1, self.state.active_power_mw // 60000)
        self.state.environment_temperature_c_x10 = int(265 + 18 * math.sin(phase / 4.0))
        self.state.environment_humidity_rh_x10 = int(545 + 40 * math.sin(phase / 5.0))
        self.state.environment_pm25_ugm3 = int(18 + 8 * (0.5 + 0.5 * math.sin(phase / 2.7)))
        self.state.environment_co2_ppm = int(520 + 35 * (0.5 + 0.5 * math.sin(phase / 6.0)))
        self.state.environment_illuminance_lux = int(3200 + 650 * math.sin(phase / 3.7))
        self.state.rs485_tx_count += ticks
        self.state.rs485_rx_count += ticks if self.state.rs485_online else 0
        self.state.rs485_last_response_ms = 18 + int(12 * (0.5 + 0.5 * math.sin(phase)))
        if self.state.rs485_online is False:
            self.state.rs485_error_count += 1
        seconds = max(1, ticks // 20)
        if (self.state.relay1_on or self.state.relay2_on) and self.state.lamp1_brightness > 0:
            self.state.open_lamp_seconds += seconds
            self.state.bright_time_seconds += seconds
            self.state.bright_time_total_seconds += seconds

    def build_telemetry_payload(self) -> dict[str, Any]:
        s = self.state
        return {
            "schema": "emqx-gateway.realboard.telemetry.v1",
            "device_id": self.config.device_id,
            "firmware_version": self.config.firmware_version,
            "business_mode": "real_board",
            "meter_hlw8112": {
                "valid": s.meter_hlw8112_valid,
                "source": "real_driver",
                "voltage_mv": s.voltage_mv,
                "current_ma": s.current_ma,
                "active_power_mw": s.active_power_mw,
                "reactive_power_mvar": s.reactive_power_mvar,
                "power_factor_x1000": s.power_factor_x1000,
                "energy_one_wh": s.energy_one_wh,
                "energy_total_wh": s.energy_total_wh,
                "pulse_count": s.pulse_count,
            },
            "environment": {
                "valid": s.environment_valid,
                "source": "real_driver",
                "temperature_c_x10": s.environment_temperature_c_x10,
                "humidity_rh_x10": s.environment_humidity_rh_x10,
                "pm25_ugm3": s.environment_pm25_ugm3,
                "co2_ppm": s.environment_co2_ppm,
                "illuminance_lux": s.environment_illuminance_lux,
            },
            "rs485": {
                "valid": s.rs485_valid,
                "online": s.rs485_online,
                "device_count": s.rs485_device_count,
                "read_period_s": s.rs485_read_period_s,
                "last_response_ms": s.rs485_last_response_ms,
                "tx_count": s.rs485_tx_count,
                "rx_count": s.rs485_rx_count,
                "error_count": s.rs485_error_count,
            },
            "lighting": self._lighting_payload(),
            "device": self._device_payload(),
            "peripherals": self._peripherals_payload(),
            "factory_test": self._factory_payload(),
            "legacy_protocol": self._legacy_payload(),
        }

    def build_status_payload(self, status: str, message: str) -> dict[str, Any]:
        return {
            "schema": "emqx-gateway.status.v1",
            "device_id": self.config.device_id,
            "status": status,
            "message": message,
        }

    def build_debug_payload(self, event: str, message: str) -> dict[str, Any]:
        return {
            "schema": "emqx-gateway.debug.v1",
            "device_id": self.config.device_id,
            "event": event,
            "message": message,
        }

    def pop_queued_event_payload(self) -> dict[str, Any] | None:
        if self.state.last_event_tick == self.state.uptime_ticks:
            return None
        if not self.state.pending_event:
            return None
        event = {
            "schema": "emqx-gateway.realboard.event.v1",
            "device_id": self.config.device_id,
            "firmware_version": self.config.firmware_version,
            "business_mode": "real_board",
            "event": self.state.pending_event,
            "level": self.state.pending_event_level,
            "uptime_ticks": self.state.uptime_ticks,
            "data": {
                "meter_hlw8112_valid": self.state.meter_hlw8112_valid,
                "environment_valid": self.state.environment_valid,
                "rs485_valid": self.state.rs485_valid,
                "rs485_online": self.state.rs485_online,
                "error_code": self.state.error_code,
            },
        }
        self.state.pending_event = ""
        self.state.pending_event_level = ""
        self.state.last_event_tick = self.state.uptime_ticks
        return event

    def handle_payload_text(self, payload_text: str) -> list[ProtocolPublish]:
        try:
            root = json.loads(payload_text)
        except Exception:
            return [
                ProtocolPublish("debug", self.build_debug_payload("invalid_json", "cmd/down payload is not valid JSON")),
                ProtocolPublish("event", self._response(None, None, False, 400, "invalid JSON")),
            ]
        if not isinstance(root, dict):
            return [ProtocolPublish("event", self._response(None, None, False, 400, "cmd must be a string"))]
        return self.handle_command_dict(root)

    def handle_command_dict(self, root: dict[str, Any]) -> list[ProtocolPublish]:
        request_id = self._request_id(root)
        cmd = root.get("cmd")
        if not isinstance(cmd, str):
            return [ProtocolPublish("event", self._response(request_id, None, False, 400, "cmd must be a string"))]

        if cmd == "ping":
            return [ProtocolPublish("event", self._response(request_id, cmd, True, 0, "ok", {"pong": True}))]
        if cmd == "get_status":
            data = {"host": self.config.broker_host, "port": self.config.broker_port, "mqtt_state": "connected"}
            data.update(self._status_fields())
            data["ota"] = self._ota_status()
            data["ntp"] = self._ntp_status()
            return [ProtocolPublish("event", self._response(request_id, cmd, True, 0, "ok", data))]
        if cmd == "get_config":
            return [ProtocolPublish("event", self._response(request_id, cmd, True, 0, "ok", self._config_payload()))]
        if cmd == "set_config":
            ok, code, message, reboot_scheduled = self._apply_config(root.get("params"))
            if ok:
                data = {"restart_required": True, "reboot_scheduled": reboot_scheduled}
                messages = [ProtocolPublish("event", self._response(request_id, cmd, True, 0, "configuration saved", data))]
                messages.append(ProtocolPublish("event", self.build_status_payload("config_saved", "configuration saved; restart required")))
                return messages
            return [ProtocolPublish("event", self._response(request_id, cmd, False, code, message))]
        if cmd == "reboot":
            self.state.reboot_requested = True
            return [
                ProtocolPublish("event", self._response(request_id, cmd, True, 0, "reboot scheduled")),
                ProtocolPublish("event", self.build_status_payload("rebooting", "reboot command accepted")),
            ]
        if cmd in ("real_set", "sim_set"):
            return [ProtocolPublish("event", self._handle_real_set(request_id, cmd, root.get("params")))]
        if cmd in ("real_event", "sim_event"):
            return [ProtocolPublish("event", self._handle_real_event(request_id, cmd, root.get("params")))]
        if cmd in ("ota_begin", "ota_chunk", "ota_end", "ota_abort", "ota_status"):
            return [ProtocolPublish("event", self._handle_ota(request_id, cmd, root.get("params")))]
        return [ProtocolPublish("event", self._response(request_id, cmd, False, 404, "unknown command"))]

    def _response(
        self,
        request_id: str | None,
        cmd: str | None,
        ok: bool,
        code: int,
        message: str,
        data: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "schema": "emqx-gateway.response.v1",
            "device_id": self.config.device_id,
            "cmd": cmd or "",
            "ok": bool(ok),
            "code": int(code),
            "message": message,
        }
        if request_id:
            payload["id"] = request_id
        if data is not None:
            payload["data"] = data
        return payload

    def _request_id(self, root: dict[str, Any]) -> str:
        value = root.get("id")
        if isinstance(value, str):
            return value
        if isinstance(value, (int, float)):
            return str(int(value))
        return ""

    def _config_payload(self) -> dict[str, Any]:
        return {
            "device_id": self.config.device_id,
            "host": self.config.broker_host,
            "port": self.config.broker_port,
            "username": self.config.username,
            "password_set": bool(self.config.password),
            "ntp_server": self.config.ntp_server,
            "tls_mode": self.config.tls_mode,
            "tls_verify_peer": self.config.tls_verify_peer,
            "topics": list(self.config.topics),
            "qos": list(self.config.qos),
        }

    def _status_fields(self) -> dict[str, Any]:
        s = self.state
        data = {
            "business_mode": "real_board",
            "firmware_version": self.config.firmware_version,
            "legacy_downlink_supported": self.config.legacy_downlink_supported,
            "address": s.address,
            "keepalive_s": s.keepalive_s,
            "error_code": s.error_code,
            "last_peripheral_cmd": s.last_peripheral_cmd,
            "meter_hlw8112_valid": s.meter_hlw8112_valid,
            "meter_voltage_mv": s.voltage_mv,
            "environment_valid": s.environment_valid,
            "environment_temperature_c_x10": s.environment_temperature_c_x10,
            "rs485_valid": s.rs485_valid,
            "rs485_online": s.rs485_online,
        }
        data["a5_pending"] = []
        return data

    def _apply_config(self, params: Any) -> tuple[bool, int, str, bool]:
        if not isinstance(params, dict):
            return False, 422, "params must be an object", False
        for name, max_len in (
            ("device_id", 32),
            ("host", 64),
            ("username", 32),
            ("password", 32),
            ("ntp_server", 64),
        ):
            if name in params and (not isinstance(params[name], str) or len(params[name]) >= max_len):
                return False, 422, f"{name} must be a string" if not isinstance(params[name], str) else f"{name} is too long", False
        if "port" in params and (not isinstance(params["port"], int) or params["port"] <= 0 or params["port"] > 65535):
            return False, 422, "port must be 1-65535", False
        if "tls_mode" in params and (not isinstance(params["tls_mode"], int) or params["tls_mode"] < 0 or params["tls_mode"] > 2):
            return False, 422, "tls_mode must be 0, 1 or 2", False
        if "topics" in params:
            topics = params["topics"]
            if not isinstance(topics, list) or len(topics) != 5 or any(not isinstance(item, str) or len(item) >= 96 for item in topics):
                return False, 422, "topics must contain five strings", False
        if "qos" in params:
            qos = params["qos"]
            if not isinstance(qos, list) or len(qos) != 5 or any(not isinstance(item, int) or item < 0 or item > 2 for item in qos):
                return False, 422, "qos must contain five numbers", False

        self.config.device_id = params.get("device_id", self.config.device_id)
        self.config.broker_host = params.get("host", self.config.broker_host)
        self.config.username = params.get("username", self.config.username)
        self.config.password = params.get("password", self.config.password)
        self.config.ntp_server = params.get("ntp_server", self.config.ntp_server)
        self.config.broker_port = params.get("port", self.config.broker_port)
        self.config.tls_mode = params.get("tls_mode", self.config.tls_mode)
        if "tls_verify_peer" in params:
            self.config.tls_verify_peer = params["tls_verify_peer"] is not False
        if "topics" in params:
            self.config.topics = list(params["topics"])
        if "qos" in params:
            self.config.qos = list(params["qos"])
        reboot = params.get("reboot") is True
        self.state.reboot_requested = reboot
        return True, 0, "configuration saved", reboot

    def _handle_real_set(self, request_id: str, cmd: str, params: Any) -> dict[str, Any]:
        if not isinstance(params, dict):
            return self._response(request_id, cmd, False, 422, "params must be an object")
        if "relay1_on" in params:
            self.state.relay1_on = params["relay1_on"] is True
        if "relay2_on" in params:
            self.state.relay2_on = params["relay2_on"] is True
        for name in ("lamp1_brightness", "lamp2_brightness"):
            if name in params:
                value = params[name]
                if not isinstance(value, int) or value < 0 or value > 255:
                    return self._response(request_id, cmd, False, 422, "real_set parameter is out of range")
                setattr(self.state, name, value)
        ignored = [name for name in ("voltage_mv", "current_ma", "temperature_c_x10", "rs485_online") if name in params]
        data = {
            "ignored_real_only_fields": ignored,
            "mode": "real_board",
            "telemetry_fields_written": False,
            "compat_alias": cmd == "sim_set",
        }
        data.update(self._status_fields())
        message = "real board command accepted; telemetry fields ignored" if cmd == "sim_set" else "real board state updated"
        return self._response(request_id, cmd, True, 0, message, data)

    def _handle_real_event(self, request_id: str, cmd: str, params: Any) -> dict[str, Any]:
        event_name = "manual_test"
        level = "info"
        if isinstance(params, dict):
            if isinstance(params.get("event"), str):
                event_name = params["event"]
            if isinstance(params.get("level"), str):
                level = params["level"]
        self.state.pending_event = event_name
        self.state.pending_event_level = level
        data = {"queued_event": event_name, "level": level, "mode": "real_board"}
        message = "real board event queued via compatibility command" if cmd == "sim_event" else "real board event queued"
        return self._response(request_id, cmd, True, 0, message, data)

    def _handle_ota(self, request_id: str, cmd: str, params: Any) -> dict[str, Any]:
        if cmd == "ota_status":
            return self._response(request_id, cmd, True, 0, "OTA status", self._ota_status())
        if cmd == "ota_abort":
            self.ota = OtaState()
            return self._response(request_id, cmd, True, 0, "OTA session aborted", self._ota_status())
        if cmd == "ota_begin":
            return self._ota_begin(request_id, cmd, params)
        if cmd == "ota_chunk":
            return self._ota_chunk(request_id, cmd, params)
        if cmd == "ota_end":
            return self._ota_end(request_id, cmd, params)
        return self._response(request_id, cmd, False, 404, "unknown OTA command")

    def _ota_begin(self, request_id: str, cmd: str, params: Any) -> dict[str, Any]:
        if not isinstance(params, dict):
            return self._response(request_id, cmd, False, 422, "ota_begin params are invalid")
        required = ("session_id", "file_len", "file_crc32", "chunk_size", "chunk_count")
        if any(name not in params for name in required) or not isinstance(params.get("session_id"), str):
            return self._response(request_id, cmd, False, 422, "ota_begin params are invalid")
        try:
            file_len = int(params["file_len"])
            file_crc32 = int(params["file_crc32"])
            chunk_size = int(params["chunk_size"])
            chunk_count = int(params["chunk_count"])
        except Exception:
            return self._response(request_id, cmd, False, 422, "ota_begin params are invalid")
        if file_len <= 0 or file_len > (APP2_END - APP2_START):
            return self._response(request_id, cmd, False, 422, "OTA image length exceeds APP2")
        if chunk_size <= 0 or chunk_size > OTA_MAX_CHUNK_SIZE or chunk_count <= 0 or chunk_count > OTA_MAX_CHUNKS:
            return self._response(request_id, cmd, False, 422, "OTA chunk settings are invalid")
        if ((chunk_count - 1) * chunk_size) >= file_len:
            return self._response(request_id, cmd, False, 422, "OTA chunk count does not match image length")
        if (chunk_count * chunk_size) < file_len:
            return self._response(request_id, cmd, False, 422, "OTA chunk count is too small")
        self.ota = OtaState(
            active=True,
            complete=False,
            session_id=params["session_id"],
            file_len=file_len,
            file_crc32=file_crc32,
            chunk_size=chunk_size,
            chunk_count=chunk_count,
        )
        return self._response(request_id, cmd, True, 0, "OTA session started", self._ota_transfer_ack("begin", 0xFFFFFFFF))

    def _ota_chunk(self, request_id: str, cmd: str, params: Any) -> dict[str, Any]:
        if not isinstance(params, dict) or not isinstance(params.get("session_id"), str) or not isinstance(params.get("data"), str):
            return self._response(request_id, cmd, False, 422, "ota_chunk params are invalid")
        if not self._same_ota_session(params["session_id"]):
            return self._response(request_id, cmd, False, 409, "OTA session mismatch")
        try:
            index = int(params["index"])
            offset = int(params["offset"])
            expected_crc = int(params["chunk_crc32"])
            data = base64.b64decode(params["data"], validate=True)
        except (KeyError, ValueError, binascii.Error):
            return self._response(request_id, cmd, False, 422, "ota_chunk params are invalid")
        if index < 0 or index >= self.ota.chunk_count or index >= OTA_MAX_CHUNKS or offset < 0 or offset >= self.ota.file_len:
            return self._response(request_id, cmd, False, 422, "OTA chunk index or offset is invalid")
        if not data or len(data) > self.ota.chunk_size or offset + len(data) > self.ota.file_len:
            return self._response(request_id, cmd, False, 422, "OTA chunk length is invalid")
        if _crc32(data) != expected_crc:
            return self._response(request_id, cmd, False, 422, "OTA chunk CRC mismatch")
        if index not in self.ota.chunks:
            self.ota.received_bytes += len(data)
        self.ota.chunks[index] = data
        return self._response(request_id, cmd, True, 0, "OTA chunk written", self._ota_transfer_ack("chunk", index))

    def _ota_end(self, request_id: str, cmd: str, params: Any) -> dict[str, Any]:
        if not isinstance(params, dict) or not isinstance(params.get("session_id"), str):
            return self._response(request_id, cmd, False, 422, "ota_end params are invalid")
        if not self._same_ota_session(params["session_id"]):
            return self._response(request_id, cmd, False, 409, "OTA session mismatch")
        if len(self.ota.chunks) != self.ota.chunk_count:
            return self._response(request_id, cmd, False, 422, "OTA chunks are incomplete")
        image = b"".join(self.ota.chunks[index] for index in range(self.ota.chunk_count))
        if len(image) != self.ota.file_len or _crc32(image) != self.ota.file_crc32:
            self.ota.last_error = "OTA image CRC mismatch"
            return self._response(request_id, cmd, False, 422, self.ota.last_error)
        self.ota.active = False
        self.ota.complete = True
        self.state.reboot_requested = True
        return self._response(request_id, cmd, True, 0, "OTA image verified; reboot scheduled", self._ota_transfer_ack("end", 0xFFFFFFFF))

    def _same_ota_session(self, session_id: str) -> bool:
        return (self.ota.active or self.ota.complete) and session_id == self.ota.session_id

    def _ota_transfer_ack(self, phase: str, index: int) -> dict[str, Any]:
        data = self._ota_status()
        data["phase"] = phase
        if index != 0xFFFFFFFF:
            data["index"] = index
        return data

    def _ota_status(self) -> dict[str, Any]:
        return {
            "active": self.ota.active,
            "complete": self.ota.complete,
            "session_id": self.ota.session_id,
            "app1_start": APP1_START,
            "app2_start": APP2_START,
            "app2_end": APP2_END,
            "upgrade_state_addr": UPGRADE_STATE_ADDR,
            "file_len": self.ota.file_len,
            "file_crc32": self.ota.file_crc32,
            "chunk_size": self.ota.chunk_size,
            "chunk_count": self.ota.chunk_count,
            "received_bytes": self.ota.received_bytes,
            "last_error": self.ota.last_error,
        }

    def _ntp_status(self) -> dict[str, Any]:
        now = time.localtime()
        octets = _parse_ipv4(self.config.ntp_server)
        return {
            "server": self.config.ntp_server,
            "state": "synced" if self.ntp_synced else "failed",
            "synced": self.ntp_synced,
            "server_ip_0": octets[0],
            "server_ip_1": octets[1],
            "server_ip_2": octets[2],
            "server_ip_3": octets[3],
            "year": now.tm_year,
            "month": now.tm_mon,
            "day": now.tm_mday,
            "hour": now.tm_hour,
            "minute": now.tm_min,
            "second": now.tm_sec,
            "last_error": "",
        }

    def _lighting_payload(self) -> dict[str, Any]:
        s = self.state
        return {
            "lamp1_brightness": s.lamp1_brightness,
            "lamp2_brightness": s.lamp2_brightness,
            "relay1_on": s.relay1_on,
            "relay2_on": s.relay2_on,
            "open_lamp_seconds": s.open_lamp_seconds,
            "bright_time_seconds": s.bright_time_seconds,
            "bright_time_total_seconds": s.bright_time_total_seconds,
        }

    def _device_payload(self) -> dict[str, Any]:
        s = self.state
        return {
            "address": s.address,
            "keepalive_s": s.keepalive_s,
            "temperature_c_x10": s.temperature_c_x10,
            "error_code": s.error_code,
            "error_renew": s.error_renew,
            "uptime_ticks": s.uptime_ticks,
        }

    def _peripherals_payload(self) -> list[dict[str, Any]]:
        return [
            self._peripheral("lamp", 0x01),
            self._peripheral("environment", 0x02),
            self._peripheral("charging_c2", 0x03),
            self._peripheral("charging_c3", 0x04),
            self._peripheral("lean", 0x05),
            self._peripheral("lora", 0x06),
            self._peripheral("gps", 0x07),
        ]

    def _peripheral(self, name: str, legacy_cmd: int) -> dict[str, Any]:
        return {
            "name": name,
            "legacy_cmd": legacy_cmd,
            "configured": self.state.peripheral_type_code != 0,
            "last_downlink": self.state.last_peripheral_cmd == legacy_cmd,
            "pending": False,
        }

    def _factory_payload(self) -> dict[str, Any]:
        s = self.state
        return {
            "test_load_power_w": s.test_load_power_w,
            "device_type": s.device_type,
            "reg_device_flag": s.reg_device_flag,
            "test_count": s.test_count,
            "test_results": s.test_results,
            "test_enable": s.test_enable,
        }

    def _legacy_payload(self) -> dict[str, Any]:
        return {
            "downlink_supported": self.config.legacy_downlink_supported,
            "product_key": self.state.legacy_product_key,
            "device_name": self.state.legacy_device_name,
            "last_peripheral_cmd": self.state.last_peripheral_cmd,
            "a5_pending": [],
        }


def _crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _parse_ipv4(text: str) -> list[int]:
    try:
        parts = [int(part) for part in text.split(".")]
    except ValueError:
        return [0, 0, 0, 0]
    if len(parts) != 4 or any(part < 0 or part > 255 for part in parts):
        return [0, 0, 0, 0]
    return parts
