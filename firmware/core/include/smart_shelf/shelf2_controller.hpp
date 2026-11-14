#pragma once

#include <cstdint>

namespace smart_shelf {

enum class ShelfState : std::uint8_t {
    Normal,
    AlertLight,
    AlertSound,
    LowStock,
    OutOfStock,
    ManualAlarm,
};

struct Shelf2Thresholds {
    int light_min{100};
    int sound_max{150};
    float low_stock_g{25.0F};
    float out_of_stock_g{1.0F};
};

struct Shelf2SensorSnapshot {
    int minimum_light{1023};
    int maximum_sound{0};
    float weight_g{0.0F};
    bool scale_available{false};
    bool assistance_requested{false};
};

enum class ThresholdError : std::uint8_t {
    None,
    LightRange,
    SoundRange,
    WeightRange,
    StockOrdering,
};

constexpr ThresholdError validate(const Shelf2Thresholds& thresholds) noexcept {
    if (thresholds.light_min < 0 || thresholds.light_min > 1023) {
        return ThresholdError::LightRange;
    }
    if (thresholds.sound_max < 0 || thresholds.sound_max > 1023) {
        return ThresholdError::SoundRange;
    }
    if (thresholds.out_of_stock_g < 0.0F || thresholds.out_of_stock_g > 100000.0F ||
        thresholds.low_stock_g < 0.0F || thresholds.low_stock_g > 100000.0F) {
        return ThresholdError::WeightRange;
    }
    if (thresholds.out_of_stock_g >= thresholds.low_stock_g) {
        return ThresholdError::StockOrdering;
    }
    return ThresholdError::None;
}

constexpr ShelfState evaluate(const Shelf2SensorSnapshot& sensors,
                              const Shelf2Thresholds& thresholds) noexcept {
    if (sensors.assistance_requested) return ShelfState::ManualAlarm;
    if (sensors.minimum_light < thresholds.light_min) return ShelfState::AlertLight;
    if (sensors.maximum_sound > thresholds.sound_max) return ShelfState::AlertSound;
    if (sensors.scale_available && sensors.weight_g <= thresholds.out_of_stock_g) {
        return ShelfState::OutOfStock;
    }
    if (sensors.scale_available && sensors.weight_g <= thresholds.low_stock_g) {
        return ShelfState::LowStock;
    }
    return ShelfState::Normal;
}

constexpr const char* to_string(ShelfState state) noexcept {
    switch (state) {
        case ShelfState::AlertLight: return "ALERT_LIGHT";
        case ShelfState::AlertSound: return "ALERT_SOUND";
        case ShelfState::LowStock: return "LOW_STOCK";
        case ShelfState::OutOfStock: return "OUT_OF_STOCK";
        case ShelfState::ManualAlarm: return "MANUAL_ALARM";
        default: return "NORMAL";
    }
}

}  // namespace smart_shelf
