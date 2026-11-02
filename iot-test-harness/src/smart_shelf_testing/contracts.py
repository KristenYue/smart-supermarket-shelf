"""MQTT topic and JSON contract validation used by automated IoT tests."""

from __future__ import annotations

import json
import math
import re
from collections.abc import Mapping
from typing import Any


DEVICE_IDS = {"smart-shelf-01", "smart-shelf-02"}
DEVICE_ID_PATTERN = re.compile(r"^smart-shelf-\d{2}$")


class ContractError(ValueError):
    """Raised when an MQTT topic or payload violates the public contract."""


def _require_mapping(payload: Any) -> Mapping[str, Any]:
    if not isinstance(payload, Mapping):
        raise ContractError("payload must be a JSON object")
    return payload


def _require_keys(payload: Mapping[str, Any], keys: set[str]) -> None:
    missing = sorted(keys - payload.keys())
    if missing:
        raise ContractError(f"missing required fields: {', '.join(missing)}")


def _number(payload: Mapping[str, Any], name: str, low: float, high: float) -> float:
    value = payload.get(name)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ContractError(f"{name} must be numeric")
    numeric = float(value)
    if not math.isfinite(numeric) or not low <= numeric <= high:
        raise ContractError(f"{name} must be between {low} and {high}")
    return numeric


def _integer(payload: Mapping[str, Any], name: str, low: int, high: int) -> int:
    value = payload.get(name)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ContractError(f"{name} must be an integer")
    if not low <= value <= high:
        raise ContractError(f"{name} must be between {low} and {high}")
    return value


def _boolean(payload: Mapping[str, Any], name: str) -> bool:
    value = payload.get(name)
    if not isinstance(value, bool):
        raise ContractError(f"{name} must be a boolean")
    return value


def validate_telemetry(payload: Any) -> dict[str, Any]:
    """Validate device telemetry and return a defensive copy."""

    data = _require_mapping(payload)
    _require_keys(data, {"deviceID", "timestamp"})
    device_id = data["deviceID"]
    if not isinstance(device_id, str) or device_id not in DEVICE_IDS:
        raise ContractError("deviceID is not an allowed Smart Shelf device")
    _integer(data, "timestamp", 1, 4_102_444_800)

    if device_id == "smart-shelf-01":
        _require_keys(
            data,
            {
                "temperatureC",
                "humidityPct",
                "pressureKPa",
                "distanceCm",
                "lightRaw",
                "customerPresent",
                "visitorCount",
                "interactionCount",
                "environmentAlert",
            },
        )
        _number(data, "temperatureC", -40, 85)
        _number(data, "humidityPct", 0, 100)
        _number(data, "pressureKPa", 30, 120)
        _number(data, "distanceCm", 0, 500)
        _integer(data, "lightRaw", 0, 1023)
        _integer(data, "visitorCount", 0, 2_147_483_647)
        _integer(data, "interactionCount", 0, 2_147_483_647)
        _boolean(data, "customerPresent")
        _boolean(data, "environmentAlert")
    else:
        _require_keys(
            data,
            {
                "weightG",
                "soundRaw",
                "lightRaw",
                "buttonPressed",
                "state",
                "uptimeMs",
                "scaleHealthy",
            },
        )
        _number(data, "weightG", -1, 100_000)
        _integer(data, "soundRaw", 0, 1023)
        _integer(data, "lightRaw", 0, 1023)
        _integer(data, "uptimeMs", 0, 4_294_967_295)
        _boolean(data, "buttonPressed")
        _boolean(data, "scaleHealthy")
        allowed_states = {
            "NORMAL",
            "ALERT_LIGHT",
            "ALERT_SOUND",
            "LOW_STOCK",
            "OUT_OF_STOCK",
            "MANUAL_ALARM",
        }
        if data["state"] not in allowed_states:
            raise ContractError("state is not supported")

    return dict(data)


def validate_control_command(topic: str, command: str) -> tuple[str, str]:
    """Validate an actuator command and return ``(device_id, command)``."""

    if not isinstance(topic, str) or not isinstance(command, str):
        raise ContractError("topic and command must be strings")
    match = re.fullmatch(r"smartshelf/cmd/(smart-shelf-\d{2})", topic)
    if not match:
        raise ContractError("command topic is malformed")
    device_id = match.group(1)
    if device_id not in DEVICE_IDS:
        raise ContractError("command targets an unknown device")
    if len(command) > 16 or any(ord(char) < 32 for char in command):
        raise ContractError("command contains unsafe characters")
    normalized = command.strip().upper()
    allowed = {"ON", "OFF", "FLASH", "AUTO"}
    if device_id == "smart-shelf-02":
        allowed.add("ACK")
    if normalized not in allowed:
        raise ContractError("command is not allowed for this device")
    return device_id, normalized


def validate_config(topic: str, raw_payload: str | Mapping[str, Any]) -> tuple[str, dict[str, float]]:
    """Validate a remote threshold update without silently accepting unknown keys."""

    match = re.fullmatch(r"smartshelf/(smart-shelf-\d{2})/config", topic)
    if not match or match.group(1) not in DEVICE_IDS:
        raise ContractError("config topic is malformed or targets an unknown device")
    device_id = match.group(1)
    if isinstance(raw_payload, str):
        try:
            payload = json.loads(raw_payload)
        except json.JSONDecodeError as error:
            raise ContractError("config payload is not valid JSON") from error
    else:
        payload = raw_payload
    data = _require_mapping(payload)
    if not data:
        raise ContractError("config payload must not be empty")

    ranges = (
        {
            "tempMinC": (-40, 85),
            "tempMaxC": (-40, 85),
            "humidityMinPct": (0, 100),
            "humidityMaxPct": (0, 100),
            "customerDistanceCm": (1, 500),
            "lightChange": (1, 1023),
        }
        if device_id == "smart-shelf-01"
        else {
            "lightMin": (0, 1023),
            "soundMax": (0, 1023),
            "lowStockG": (0, 100_000),
            "outOfStockG": (0, 100_000),
        }
    )
    unknown = sorted(set(data) - set(ranges))
    if unknown:
        raise ContractError(f"unknown config fields: {', '.join(unknown)}")

    normalized: dict[str, float] = {}
    for key, value in data.items():
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ContractError(f"{key} must be numeric")
        low, high = ranges[key]
        numeric = float(value)
        if not math.isfinite(numeric) or not low <= numeric <= high:
            raise ContractError(f"{key} must be between {low} and {high}")
        normalized[key] = numeric

    if normalized.get("tempMinC", -40) >= normalized.get("tempMaxC", 85):
        raise ContractError("tempMinC must be lower than tempMaxC")
    if normalized.get("humidityMinPct", 0) >= normalized.get("humidityMaxPct", 100):
        raise ContractError("humidityMinPct must be lower than humidityMaxPct")
    if normalized.get("outOfStockG", 0) >= normalized.get("lowStockG", 100_000):
        raise ContractError("outOfStockG must be lower than lowStockG")
    return device_id, normalized
