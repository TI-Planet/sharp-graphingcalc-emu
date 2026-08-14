#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace el9950
{
    inline constexpr std::uint32_t kStateVersion = 6;
    std::uint32_t crc32(std::span<const std::uint8_t> bytes);
} // namespace el9950
