#include "smart_shelf/fixed_outbox.hpp"
#include "smart_shelf/shelf2_controller.hpp"

#include <cstring>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_state_priority_and_boundaries() {
    using namespace smart_shelf;
    const Shelf2Thresholds thresholds{};
    Shelf2SensorSnapshot sensors{};
    sensors.scale_available = true;
    sensors.weight_g = 80.0F;
    expect(evaluate(sensors, thresholds) == ShelfState::Normal, "normal snapshot");

    sensors.weight_g = 25.0F;
    expect(evaluate(sensors, thresholds) == ShelfState::LowStock,
           "low-stock inclusive boundary");
    sensors.weight_g = 1.0F;
    expect(evaluate(sensors, thresholds) == ShelfState::OutOfStock,
           "out-of-stock inclusive boundary");

    sensors.maximum_sound = 151;
    expect(evaluate(sensors, thresholds) == ShelfState::AlertSound,
           "sound alert outranks inventory");
    sensors.minimum_light = 99;
    expect(evaluate(sensors, thresholds) == ShelfState::AlertLight,
           "light alert outranks sound");
    sensors.assistance_requested = true;
    expect(evaluate(sensors, thresholds) == ShelfState::ManualAlarm,
           "human assistance has highest priority");
}

void test_threshold_validation() {
    using namespace smart_shelf;
    expect(validate(Shelf2Thresholds{}) == ThresholdError::None, "default thresholds valid");

    Shelf2Thresholds invalid{};
    invalid.out_of_stock_g = 30.0F;
    invalid.low_stock_g = 20.0F;
    expect(validate(invalid) == ThresholdError::StockOrdering,
           "out-of-stock threshold must be lower than low-stock threshold");
    invalid = Shelf2Thresholds{};
    invalid.light_min = -1;
    expect(validate(invalid) == ThresholdError::LightRange, "light threshold range");
    invalid = Shelf2Thresholds{};
    invalid.low_stock_g = -1.0F;
    expect(validate(invalid) == ThresholdError::WeightRange, "low-stock threshold range");
    invalid = Shelf2Thresholds{};
    invalid.out_of_stock_g = 100001.0F;
    expect(validate(invalid) == ThresholdError::WeightRange, "out-of-stock threshold range");
}

void test_fixed_outbox() {
    using smart_shelf::EnqueueResult;
    smart_shelf::FixedOutbox<2, 16, 32> outbox;
    expect(outbox.push("telemetry", "one") == EnqueueResult::Enqueued, "first enqueue");
    expect(outbox.push("telemetry", "two") == EnqueueResult::Enqueued, "second enqueue");
    expect(outbox.push("telemetry", "three") == EnqueueResult::DroppedOldest,
           "full queue drops oldest frame explicitly");
    expect(outbox.size() == 2 && outbox.dropped() == 1, "bounded queue counters");
    expect(std::strcmp(outbox.front()->payload.data(), "two") == 0,
           "oldest retained frame remains first");
    expect(outbox.pop(), "pop succeeds");
    expect(std::strcmp(outbox.front()->payload.data(), "three") == 0, "FIFO after drop");

    const char oversized[] = "payload-that-is-intentionally-longer-than-the-buffer";
    expect(outbox.push("telemetry", oversized) == EnqueueResult::RejectedOversize,
           "oversize frame rejected");
    expect(outbox.rejected() == 1, "oversize rejection counted");
}

}  // namespace

int main() {
    test_state_priority_and_boundaries();
    test_threshold_validation();
    test_fixed_outbox();
    if (failures == 0) {
        std::cout << "All Smart Shelf core tests passed\n";
        return 0;
    }
    return 1;
}
