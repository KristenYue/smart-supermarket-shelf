from __future__ import annotations

import pytest

from smart_shelf_testing import ContractError, validate_config, validate_control_command


@pytest.mark.parametrize("command", ["ON", "off", " Flash ", "AUTO"])
def test_common_commands_are_normalized(command):
    assert validate_control_command("smartshelf/cmd/smart-shelf-01", command)[1] == command.strip().upper()


def test_ack_is_allowed_only_for_inventory_shelf():
    assert validate_control_command("smartshelf/cmd/smart-shelf-02", "ACK") == ("smart-shelf-02", "ACK")
    with pytest.raises(ContractError, match="not allowed"):
        validate_control_command("smartshelf/cmd/smart-shelf-01", "ACK")


@pytest.mark.parametrize(
    "topic",
    [
        "smartshelf/cmd/smart-shelf-03",
        "smartshelf/cmd/+/",
        "smartshelf/smart-shelf-01/cmd",
        "smartshelf/cmd/../../admin",
    ],
)
def test_command_topic_cannot_escape_device_namespace(topic):
    with pytest.raises(ContractError):
        validate_control_command(topic, "ON")


@pytest.mark.parametrize("command", ["RESET", "ON\nADMIN", "A" * 17, ""])
def test_unknown_or_unsafe_command_is_rejected(command):
    with pytest.raises(ContractError):
        validate_control_command("smartshelf/cmd/smart-shelf-01", command)


def test_shelf1_config_accepts_safe_threshold_update():
    device, config = validate_config(
        "smartshelf/smart-shelf-01/config",
        '{"tempMinC": 1, "tempMaxC": 9, "customerDistanceCm": 90}',
    )
    assert device == "smart-shelf-01"
    assert config["tempMaxC"] == 9.0


def test_shelf2_config_accepts_inventory_thresholds():
    device, config = validate_config(
        "smartshelf/smart-shelf-02/config",
        {"outOfStockG": 1, "lowStockG": 25},
    )
    assert device == "smart-shelf-02"
    assert config == {"outOfStockG": 1.0, "lowStockG": 25.0}


@pytest.mark.parametrize(
    ("topic", "payload", "error"),
    [
        ("smartshelf/smart-shelf-03/config", {"lightMin": 2}, "unknown device"),
        ("smartshelf/smart-shelf-01/config", "not-json", "valid JSON"),
        ("smartshelf/smart-shelf-01/config", {}, "must not be empty"),
        ("smartshelf/smart-shelf-01/config", {"admin": 1}, "unknown config"),
        ("smartshelf/smart-shelf-01/config", {"tempMinC": 10, "tempMaxC": 2}, "lower"),
        ("smartshelf/smart-shelf-02/config", {"outOfStockG": 30, "lowStockG": 20}, "lower"),
        ("smartshelf/smart-shelf-02/config", {"lightMin": -1}, "between"),
    ],
)
def test_unsafe_config_is_rejected(topic, payload, error):
    with pytest.raises(ContractError, match=error):
        validate_config(topic, payload)
