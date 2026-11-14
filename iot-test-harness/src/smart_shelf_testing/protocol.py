"""Stateful protocol helpers for duplicate, ordering and reconnect tests."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from enum import Enum
from typing import Any

from .contracts import validate_telemetry


class IngestResult(str, Enum):
    ACCEPTED = "accepted"
    DUPLICATE = "duplicate"
    OUT_OF_ORDER = "out_of_order"
    PAYLOAD_CONFLICT = "payload_conflict"


def reconnect_delay_seconds(attempt: int, *, maximum: int = 30) -> int:
    """Return capped exponential backoff used by reconnect tests."""

    if attempt < 0:
        raise ValueError("attempt must be non-negative")
    return min(maximum, 2**attempt)


@dataclass(frozen=True)
class StoredTelemetry:
    device_id: str
    boot_id: int
    sequence: int
    timestamp: int
    fingerprint: str
    payload: dict[str, Any]


class TelemetryGateway:
    """In-memory reference gateway for deterministic device-cloud tests."""

    def __init__(self) -> None:
        self._latest_sequence: dict[tuple[str, int], int] = {}
        self._fingerprints: dict[tuple[str, int, int], str] = {}
        self.records: list[StoredTelemetry] = []

    def ingest(self, payload: dict[str, Any]) -> IngestResult:
        validated = validate_telemetry(payload)
        device_id = validated["deviceID"]
        boot_id = validated["bootId"]
        sequence = validated["sequence"]
        timestamp = validated["timestamp"]
        canonical = json.dumps(validated, sort_keys=True, separators=(",", ":"))
        fingerprint = hashlib.sha256(canonical.encode("utf-8")).hexdigest()
        session = (device_id, boot_id)
        identity = (device_id, boot_id, sequence)

        if identity in self._fingerprints:
            if self._fingerprints[identity] == fingerprint:
                return IngestResult.DUPLICATE
            return IngestResult.PAYLOAD_CONFLICT
        if sequence < self._latest_sequence.get(session, 0):
            return IngestResult.OUT_OF_ORDER

        self._fingerprints[identity] = fingerprint
        self._latest_sequence[session] = sequence
        self.records.append(
            StoredTelemetry(device_id, boot_id, sequence, timestamp, fingerprint, validated)
        )
        return IngestResult.ACCEPTED

    def latest(self, device_id: str) -> dict[str, Any] | None:
        matches = [record for record in self.records if record.device_id == device_id]
        return dict(matches[-1].payload) if matches else None
