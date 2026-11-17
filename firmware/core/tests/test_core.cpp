#include "smart_shelf/fixed_outbox.hpp"
#include "smart_shelf/config_version.hpp"
#include "smart_shelf/reconnect_backoff.hpp"
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
    expect(outbox.size() == 1 && std::strcmp(outbox.front()->payload.data(), "three") == 0,
           "rejected frame leaves queued data intact");
    expect(outbox.push("telemetry", "four") == EnqueueResult::Enqueued, "enqueue after wrap");
    expect(outbox.pop(), "pop retained head");
    expect(std::strcmp(outbox.front()->payload.data(), "four") == 0, "FIFO after wrap");
    expect(outbox.pop() && !outbox.pop() && outbox.front() == nullptr, "empty queue boundary");
    using FirmwareOutbox = smart_shelf::FixedOutbox<4, 96, 512>;
    std::cout << "Host ABI firmware outbox bytes: " << sizeof(FirmwareOutbox) << '\n';
}

}  // namespace

int main() {
    using smart_shelf::VersionDecision;
    smart_shelf::ConfigVersion version;
    std::uint32_t requested = 0;
    expect(version.check("{}", requested) == VersionDecision::Apply, "legacy configuration on boot");
    expect(version.check(R"({"configVersion" : 42})", requested) == VersionDecision::Apply &&
           requested == 42, "new version is eligible");
    version.effective = requested;
    expect(version.check(R"({"configVersion":42})", requested) == VersionDecision::Stale, "duplicate");
    expect(version.check(R"({"configVersion":41})", requested) == VersionDecision::Stale, "out of order");
    expect(version.check("{}", requested) == VersionDecision::Stale, "legacy cannot overwrite a version");
    expect(version.check(R"({"configVersion":4294967296})", requested) == VersionDecision::Invalid, "overflow");
    expect(version.check(R"({"configVersion":44.5})", requested) == VersionDecision::Invalid, "fraction");
    smart_shelf::ReconnectBackoff reconnect;
    expect(reconnect.ready(0), "initial attempt is immediately eligible");
    reconnect.failed(UINT32_MAX - 499U);
    expect(!reconnect.ready(499), "retry interval has not elapsed after rollover");
    expect(reconnect.ready(500), "retry becomes eligible after rollover");
    for (int i = 0; i < 10; ++i) reconnect.failed(500);
    expect(!reconnect.ready(30499) && reconnect.ready(30500), "retry interval caps at 30s");
    reconnect.connected();
    expect(reconnect.ready(501), "successful connection resets backoff");
    test_state_priority_and_boundaries();
    test_threshold_validation();
    test_fixed_outbox();
    if (failures == 0) {
        std::cout << "All Smart Shelf core tests passed\n";
        return 0;
    }
    return 1;
}
