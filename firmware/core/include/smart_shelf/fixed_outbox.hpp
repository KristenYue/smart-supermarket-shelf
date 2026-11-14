#pragma once

#include <array>
#include <cstddef>
#include <cstring>

namespace smart_shelf {

enum class EnqueueResult {
    Enqueued,
    DroppedOldest,
    RejectedOversize,
};

template <std::size_t Capacity, std::size_t TopicBytes, std::size_t PayloadBytes>
class FixedOutbox {
    static_assert(Capacity > 0, "outbox capacity must be positive");
    static_assert(TopicBytes > 1, "topic buffer must include a terminator");
    static_assert(PayloadBytes > 1, "payload buffer must include a terminator");

public:
    struct Frame {
        std::array<char, TopicBytes> topic{};
        std::array<char, PayloadBytes> payload{};
        bool retained{false};
        int qos{1};
    };

    EnqueueResult push(const char* topic, const char* payload, bool retained = false,
                       int qos = 1) noexcept {
        if (topic == nullptr || payload == nullptr) {
            ++rejected_;
            return EnqueueResult::RejectedOversize;
        }
        const std::size_t topic_length = std::strlen(topic);
        const std::size_t payload_length = std::strlen(payload);
        if (topic_length >= TopicBytes || payload_length >= PayloadBytes) {
            ++rejected_;
            return EnqueueResult::RejectedOversize;
        }

        EnqueueResult result = EnqueueResult::Enqueued;
        if (size_ == Capacity) {
            head_ = (head_ + 1) % Capacity;
            --size_;
            ++dropped_;
            result = EnqueueResult::DroppedOldest;
        }

        const std::size_t tail = (head_ + size_) % Capacity;
        Frame& frame = frames_[tail];
        std::memcpy(frame.topic.data(), topic, topic_length);
        frame.topic[topic_length] = '\0';
        std::memcpy(frame.payload.data(), payload, payload_length);
        frame.payload[payload_length] = '\0';
        frame.retained = retained;
        frame.qos = qos;
        ++size_;
        return result;
    }

    const Frame* front() const noexcept {
        return size_ == 0 ? nullptr : &frames_[head_];
    }

    bool pop() noexcept {
        if (size_ == 0) return false;
        head_ = (head_ + 1) % Capacity;
        --size_;
        return true;
    }

    std::size_t size() const noexcept { return size_; }
    constexpr std::size_t capacity() const noexcept { return Capacity; }
    std::size_t dropped() const noexcept { return dropped_; }
    std::size_t rejected() const noexcept { return rejected_; }

private:
    std::array<Frame, Capacity> frames_{};
    std::size_t head_{0};
    std::size_t size_{0};
    std::size_t dropped_{0};
    std::size_t rejected_{0};
};

}  // namespace smart_shelf
