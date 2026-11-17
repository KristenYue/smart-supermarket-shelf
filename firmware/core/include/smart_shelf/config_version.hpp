#pragma once

#include <cstdint>
#include <string_view>

namespace smart_shelf {

enum class VersionDecision { Apply, Invalid, Stale };

struct ConfigVersion {
    std::uint32_t effective{0};

    VersionDecision check(std::string_view message, std::uint32_t& requested) const {
        const std::string_view key{"\"configVersion\""};
        const auto field = message.find(key);
        requested = 0;
        if (field == std::string_view::npos) {
            return effective == 0 ? VersionDecision::Apply : VersionDecision::Stale;
        }
        auto cursor = message.find_first_not_of(" \t\r\n", field + key.size());
        if (cursor == std::string_view::npos || message[cursor] != ':' ||
            message.find(key, field + key.size()) != std::string_view::npos) {
            return VersionDecision::Invalid;
        }
        cursor = message.find_first_not_of(" \t\r\n", cursor + 1);
        if (cursor == std::string_view::npos || message[cursor] < '1' || message[cursor] > '9') {
            return VersionDecision::Invalid;
        }
        for (; cursor < message.size() && message[cursor] >= '0' && message[cursor] <= '9'; ++cursor) {
            const auto digit = static_cast<std::uint32_t>(message[cursor] - '0');
            if (requested > (UINT32_MAX - digit) / 10) return VersionDecision::Invalid;
            requested = requested * 10 + digit;
        }
        cursor = message.find_first_not_of(" \t\r\n", cursor);
        if (cursor == std::string_view::npos || (message[cursor] != ',' && message[cursor] != '}')) {
            return VersionDecision::Invalid;
        }
        return requested > effective ? VersionDecision::Apply : VersionDecision::Stale;
    }
};

}  // namespace smart_shelf
