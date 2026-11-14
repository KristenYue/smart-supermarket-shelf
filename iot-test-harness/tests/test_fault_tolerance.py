from __future__ import annotations

import pytest

from smart_shelf_testing import IngestResult, TelemetryGateway, reconnect_delay_seconds


@pytest.mark.parametrize(
    ("attempt", "expected"),
    [(0, 1), (1, 2), (2, 4), (3, 8), (4, 16), (5, 30), (10, 30)],
)
def test_reconnect_backoff_is_exponential_and_capped(attempt, expected):
    assert reconnect_delay_seconds(attempt) == expected


def test_negative_reconnect_attempt_is_rejected():
    with pytest.raises(ValueError):
        reconnect_delay_seconds(-1)


def test_duplicate_message_is_idempotent(shelf1_payload):
    gateway = TelemetryGateway()
    assert gateway.ingest(shelf1_payload) is IngestResult.ACCEPTED
    assert gateway.ingest(shelf1_payload) is IngestResult.DUPLICATE
    assert len(gateway.records) == 1


def test_out_of_order_message_is_not_persisted(shelf1_payload):
    gateway = TelemetryGateway()
    shelf1_payload.update(sequence=2, messageId="smart-shelf-01:1001:2")
    assert gateway.ingest(shelf1_payload) is IngestResult.ACCEPTED
    older = dict(shelf1_payload, sequence=1, messageId="smart-shelf-01:1001:1")
    assert gateway.ingest(older) is IngestResult.OUT_OF_ORDER
    assert len(gateway.records) == 1


def test_same_sequence_with_different_payload_is_conflict(shelf1_payload):
    gateway = TelemetryGateway()
    assert gateway.ingest(shelf1_payload) is IngestResult.ACCEPTED
    conflicting = dict(shelf1_payload, temperatureC=7.5)
    assert gateway.ingest(conflicting) is IngestResult.PAYLOAD_CONFLICT


def test_device_ordering_is_tracked_independently(shelf1_payload, shelf2_payload):
    gateway = TelemetryGateway()
    assert gateway.ingest(shelf1_payload) is IngestResult.ACCEPTED
    shelf2_payload["timestamp"] -= 10
    assert gateway.ingest(shelf2_payload) is IngestResult.ACCEPTED
    assert len(gateway.records) == 2


def test_sequence_ordering_survives_unsynchronized_clock(shelf1_payload):
    gateway = TelemetryGateway()
    first = dict(
        shelf1_payload,
        sequence=1,
        messageId="smart-shelf-01:1001:1",
        timestamp=0,
        timeSynchronized=False,
    )
    second = dict(
        shelf1_payload,
        sequence=2,
        messageId="smart-shelf-01:1001:2",
        timestamp=1_800_000_000,
        timeSynchronized=True,
    )
    assert gateway.ingest(first) is IngestResult.ACCEPTED
    assert gateway.ingest(second) is IngestResult.ACCEPTED


def test_new_boot_session_may_restart_sequence(shelf1_payload):
    gateway = TelemetryGateway()
    first_boot = dict(
        shelf1_payload,
        sequence=20,
        messageId="smart-shelf-01:1001:20",
    )
    after_reboot = dict(
        shelf1_payload,
        bootId=1002,
        sequence=1,
        messageId="smart-shelf-01:1002:1",
        timestamp=shelf1_payload["timestamp"] + 1,
    )
    assert gateway.ingest(first_boot) is IngestResult.ACCEPTED
    assert gateway.ingest(after_reboot) is IngestResult.ACCEPTED


def test_latest_returns_defensive_copy(shelf2_payload):
    gateway = TelemetryGateway()
    gateway.ingest(shelf2_payload)
    latest = gateway.latest("smart-shelf-02")
    latest["weightG"] = 0
    assert gateway.latest("smart-shelf-02")["weightG"] == 80.0
