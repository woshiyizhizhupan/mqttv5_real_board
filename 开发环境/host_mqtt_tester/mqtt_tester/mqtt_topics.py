from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class MqttTopics:
    telemetry_up: str
    event_up: str
    status_up: str
    cmd_down: str
    debug_up: str

    def as_list(self) -> list[str]:
        return [
            self.telemetry_up,
            self.event_up,
            self.status_up,
            self.cmd_down,
            self.debug_up,
        ]

    def as_named_pairs(self) -> list[tuple[str, str]]:
        return [
            ("telemetry_up", self.telemetry_up),
            ("event_up", self.event_up),
            ("status_up", self.status_up),
            ("cmd_down", self.cmd_down),
            ("debug_up", self.debug_up),
        ]


def build_default_topics(product_key: str = "local_pk", device_name: str = "hc32f460_dev") -> MqttTopics:
    product_key = _clean_segment(product_key, "product_key")
    device_name = _clean_segment(device_name, "device_name")
    prefix = f"/{product_key}/{device_name}/user"
    topics = MqttTopics(
        telemetry_up=f"{prefix}/telemetry/up",
        event_up=f"{prefix}/event/up",
        status_up=f"{prefix}/status/up",
        cmd_down=f"{prefix}/cmd/down",
        debug_up=f"{prefix}/debug/up",
    )
    validate_topics(topics.as_list())
    return topics


def validate_topics(topics: list[str]) -> None:
    if len(topics) != 5:
        raise ValueError("exactly five topics are required")
    if any(not topic or not topic.strip() for topic in topics):
        raise ValueError("topics must not be empty")
    if len(set(topics)) != len(topics):
        raise ValueError("topics must be unique")


def _clean_segment(value: str, name: str) -> str:
    cleaned = value.strip()
    if not cleaned:
        raise ValueError(f"{name} must not be empty")
    if "/" in cleaned or "#" in cleaned or "+" in cleaned:
        raise ValueError(f"{name} must be a plain MQTT topic segment")
    return cleaned
