from __future__ import annotations

import math

import pytest

from smart_shelf_testing import ContractError, validate_telemetry


def test_shelf1_valid_payload_is_accepted(shelf1_payload):
    assert validate_telemetry(shelf1_payload) == shelf1_payload


def test_shelf2_valid_payload_is_accepted(shelf2_payload):
    assert validate_telemetry(shelf2_payload) == shelf2_payload


@pytest.mark.parametrize("field", ["deviceID", "timestamp", "temperatureC", "humidityPct", "pressureKPa"])
def test_shelf1_rejects_missing_required_field(shelf1_payload, field):
    shelf1_payload.pop(field)
    with pytest.raises(ContractError, match="missing required fields"):
        validate_telemetry(shelf1_payload)


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("temperatureC", -40.1),
        ("temperatureC", 85.1),
        ("humidityPct", -0.1),
        ("humidityPct", 100.1),
        ("pressureKPa", math.nan),
        ("distanceCm", 501),
        ("lightRaw", 1024),
        ("visitorCount", -1),
    ],
)
def test_shelf1_rejects_out_of_range_sensor_data(shelf1_payload, field, value):
    shelf1_payload[field] = value
    with pytest.raises(ContractError):
        validate_telemetry(shelf1_payload)


@pytest.mark.parametrize("field", ["customerPresent", "environmentAlert"])
def test_shelf1_rejects_non_boolean_flags(shelf1_payload, field):
    shelf1_payload[field] = 1
    with pytest.raises(ContractError, match="must be a boolean"):
        validate_telemetry(shelf1_payload)


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("weightG", -1.1),
        ("soundRaw", -1),
        ("soundRaw", 1024),
        ("lightRaw", 1024),
        ("uptimeMs", -1),
        ("state", "UNKNOWN"),
    ],
)
def test_shelf2_rejects_invalid_sensor_or_state_data(shelf2_payload, field, value):
    shelf2_payload[field] = value
    with pytest.raises(ContractError):
        validate_telemetry(shelf2_payload)


@pytest.mark.parametrize("device_id", ["smart-shelf-03", "../../admin", "", None])
def test_unknown_or_malformed_device_is_rejected(shelf1_payload, device_id):
    shelf1_payload["deviceID"] = device_id
    with pytest.raises(ContractError, match="allowed"):
        validate_telemetry(shelf1_payload)


def test_input_payload_is_not_mutated(shelf1_payload):
    validated = validate_telemetry(shelf1_payload)
    validated["temperatureC"] = 99
    assert shelf1_payload["temperatureC"] == 5.0
