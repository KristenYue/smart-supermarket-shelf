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
    assert gateway.ingest(shelf1_payload) is IngestResult.ACCEPTED
    older = dict(shelf1_payload, timestamp=shelf1_payload["timestamp"] - 1)
    assert gateway.ingest(older) is IngestResult.OUT_OF_ORDER
    assert len(gateway.records) == 1


def test_same_timestamp_with_different_payload_is_conflict(shelf1_payload):
    gateway = TelemetryGateway()
    assert gateway.ingest(shelf1_payload) is IngestResult.ACCEPTED
    conflicting = dict(shelf1_payload, temperatureC=7.5)
    assert gateway.ingest(conflicting) is IngestResult.TIMESTAMP_CONFLICT


def test_device_ordering_is_tracked_independently(shelf1_payload, shelf2_payload):
    gateway = TelemetryGateway()
    assert gateway.ingest(shelf1_payload) is IngestResult.ACCEPTED
    shelf2_payload["timestamp"] -= 10
    assert gateway.ingest(shelf2_payload) is IngestResult.ACCEPTED
    assert len(gateway.records) == 2


def test_latest_returns_defensive_copy(shelf2_payload):
    gateway = TelemetryGateway()
    gateway.ingest(shelf2_payload)
    latest = gateway.latest("smart-shelf-02")
    latest["weightG"] = 0
    assert gateway.latest("smart-shelf-02")["weightG"] == 80.0
