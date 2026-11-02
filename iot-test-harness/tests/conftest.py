from __future__ import annotations

import pytest


@pytest.fixture
def shelf1_payload() -> dict[str, object]:
    return {
        "deviceID": "smart-shelf-01",
        "timestamp": 1_800_000_000,
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
        "timestamp": 1_800_000_000,
        "weightG": 80.0,
        "soundRaw": 20,
        "lightRaw": 500,
        "buttonPressed": False,
        "state": "NORMAL",
        "uptimeMs": 20_000,
        "scaleHealthy": True,
    }
