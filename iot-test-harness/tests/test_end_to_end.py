from __future__ import annotations

import pytest

from smart_shelf_testing import IngestResult, TelemetryGateway, validate_control_command


def test_two_devices_can_report_without_cross_talk(shelf1_payload, shelf2_payload):
    gateway = TelemetryGateway()
    assert gateway.ingest(shelf1_payload) is IngestResult.ACCEPTED
    assert gateway.ingest(shelf2_payload) is IngestResult.ACCEPTED
    assert gateway.latest("smart-shelf-01")["temperatureC"] == 5.0
    assert gateway.latest("smart-shelf-02")["weightG"] == 80.0


@pytest.mark.parametrize(
    ("weight", "expected_state"),
    [(80.0, "NORMAL"), (25.0, "LOW_STOCK"), (1.0, "OUT_OF_STOCK")],
)
def test_inventory_state_is_preserved_across_device_cloud_boundary(shelf2_payload, weight, expected_state):
    gateway = TelemetryGateway()
    shelf2_payload.update(weightG=weight, state=expected_state)
    assert gateway.ingest(shelf2_payload) is IngestResult.ACCEPTED
    assert gateway.latest("smart-shelf-02")["state"] == expected_state


def test_remote_command_targets_exact_device_only():
    target, command = validate_control_command("smartshelf/cmd/smart-shelf-02", "ack")
    assert target == "smart-shelf-02"
    assert command == "ACK"


def test_reconnected_device_can_continue_with_new_timestamp(shelf1_payload):
    gateway = TelemetryGateway()
    gateway.ingest(shelf1_payload)
    after_reconnect = dict(shelf1_payload, timestamp=shelf1_payload["timestamp"] + 5, customerPresent=False)
    assert gateway.ingest(after_reconnect) is IngestResult.ACCEPTED
    assert gateway.latest("smart-shelf-01")["customerPresent"] is False
