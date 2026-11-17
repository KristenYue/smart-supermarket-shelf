#pragma once

#include <cstdint>

namespace smart_shelf {

class ReconnectBackoff {
public:
    bool ready(std::uint32_t now) const noexcept {
        return wait_ms_ == 0 || now - attempted_at_ >= wait_ms_;
    }

    void failed(std::uint32_t now) noexcept {
        attempted_at_ = now;
        wait_ms_ = wait_ms_ == 0 ? 1000 : wait_ms_ * 2;
        if (wait_ms_ > 30000) wait_ms_ = 30000;
    }

    void connected() noexcept { wait_ms_ = 0; }

private:
    std::uint32_t attempted_at_{0};
    std::uint32_t wait_ms_{0};
};

}  // namespace smart_shelf
