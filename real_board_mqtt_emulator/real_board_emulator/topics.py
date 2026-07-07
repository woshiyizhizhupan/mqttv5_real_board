from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class CityDeviceTopics:
    telemetry_up: str
    cmd_down: str
    event_up: str
    ota_down: str
    debug_up: str

    def downlink_topics(self) -> list[str]:
        return [self.cmd_down, self.ota_down]

    def uplink_topics(self) -> list[str]:
        return [self.telemetry_up, self.event_up, self.debug_up]


def build_city_device_topics(
    city_id: str = "tjw",
    pole_id: str = "pole001",
    device_name: str = "GM400-452089",
    device_id: str | None = None,
) -> CityDeviceTopics:
    city = _clean_segment(city_id, "city_id")
    pole = _clean_segment(pole_id, "pole_id")
    name = _clean_segment(device_name, "device_name")
    ota_device = _clean_segment(device_id or device_name, "device_id")
    base = f"city/{city}/pole/{pole}/device/{name}"
    topics = CityDeviceTopics(
        telemetry_up=base + "/",
        cmd_down=base + "/get",
        event_up=base + "/event",
        ota_down=f"city/{city}/pole/{pole}/device/{ota_device}/ota",
        debug_up=base + "/debug",
    )
    _validate_unique([topics.telemetry_up, topics.cmd_down, topics.event_up, topics.ota_down, topics.debug_up])
    return topics


def _clean_segment(value: str, name: str) -> str:
    cleaned = str(value).strip()
    if not cleaned:
        raise ValueError(f"{name} must not be empty")
    if any(ch in cleaned for ch in ("/", "#", "+")):
        raise ValueError(f"{name} must be a plain MQTT topic segment")
    return cleaned


def _validate_unique(topics: list[str]) -> None:
    if len(topics) != 5:
        raise ValueError("exactly five topics are required")
    if len(set(topics)) != len(topics):
        raise ValueError("topics must be unique")

