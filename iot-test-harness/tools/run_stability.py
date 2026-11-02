"""Run a deterministic, hardware-independent telemetry stability scenario."""

from __future__ import annotations

import argparse
import json
import random
import time
from collections import Counter

from smart_shelf_testing import TelemetryGateway


def shelf1(timestamp: int, index: int) -> dict[str, object]:
    return {
        "deviceID": "smart-shelf-01",
        "timestamp": timestamp,
        "temperatureC": 5.0 + (index % 10) / 10,
        "humidityPct": 45.0 + index % 20,
        "pressureKPa": 101.3,
        "distanceCm": 50.0 + index % 100,
        "lightRaw": 300 + index % 500,
        "customerPresent": index % 4 == 0,
        "visitorCount": index // 20,
        "interactionCount": index // 40,
        "environmentAlert": False,
    }


def shelf2(timestamp: int, index: int) -> dict[str, object]:
    weight = float(100 - index % 80)
    return {
        "deviceID": "smart-shelf-02",
        "timestamp": timestamp,
        "weightG": weight,
        "soundRaw": index % 200,
        "lightRaw": 200 + index % 700,
        "buttonPressed": False,
        "state": "LOW_STOCK" if weight <= 25 else "NORMAL",
        "uptimeMs": index * 5_000,
        "scaleHealthy": True,
    }


def run(message_count: int, seed: int) -> dict[str, object]:
    random.seed(seed)
    gateway = TelemetryGateway()
    counts: Counter[str] = Counter()
    last_payload: dict[str, dict[str, object]] = {}
    started = time.perf_counter()

    for index in range(message_count):
        device_id = "smart-shelf-01" if index % 2 == 0 else "smart-shelf-02"
        timestamp = 1_800_000_000 + index
        payload = shelf1(timestamp, index) if device_id.endswith("01") else shelf2(timestamp, index)
        roll = random.random()
        if roll < 0.02 and device_id in last_payload:
            payload = dict(last_payload[device_id])
        elif roll < 0.03 and device_id in last_payload:
            payload["timestamp"] = int(last_payload[device_id]["timestamp"]) - 1
        result = gateway.ingest(payload)
        counts[result.value] += 1
        if result.value == "accepted":
            last_payload[device_id] = dict(payload)

    elapsed = time.perf_counter() - started
    return {
        "seed": seed,
        "inputMessages": message_count,
        "persistedMessages": len(gateway.records),
        "results": dict(sorted(counts.items())),
        "elapsedSeconds": round(elapsed, 4),
        "throughputMessagesPerSecond": round(message_count / elapsed, 1),
        "dataIntegrity": len(gateway.records) == counts["accepted"],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--messages", type=int, default=50_000)
    parser.add_argument("--seed", type=int, default=20261102)
    args = parser.parse_args()
    if args.messages <= 0:
        parser.error("--messages must be positive")
    report = run(args.messages, args.seed)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if not report["dataIntegrity"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
