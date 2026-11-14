from __future__ import annotations

import pytest


@pytest.fixture
def shelf1_payload() -> dict[str, object]:
    return {
        "deviceID": "smart-shelf-01",
        "schemaVersion": "2.0",
        "messageId": "smart-shelf-01:1001:1",
        "bootId": 1001,
        "sequence": 1,
        "timestamp": 1_800_000_000,
        "timeSynchronized": True,
        "temperatureC": 5.0,
        "humidityPct": 50.0,
        "pressureKPa": 101.3,
        "distanceCm": 75.0,
        "lightRaw": 480,
        "customerPresent": True,
        "visitorCount": 10,
        "interactionCount": 4,
        "environmentAlert": False,
    }


@pytest.fixture
def shelf2_payload() -> dict[str, object]:
    return {
        "deviceID": "smart-shelf-02",
        "schemaVersion": "2.0",
        "messageId": "smart-shelf-02:2001:1",
        "bootId": 2001,
        "sequence": 1,
        "timestamp": 1_800_000_000,
        "timeSynchronized": True,
        "weightG": 80.0,
        "soundRaw": 20,
        "lightRaw": 500,
        "buttonPressed": False,
        "state": "NORMAL",
        "uptimeMs": 20_000,
        "scaleHealthy": True,
    }
