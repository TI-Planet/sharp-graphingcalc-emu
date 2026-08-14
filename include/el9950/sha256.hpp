#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace el9950
{
    using Sha256Digest = std::array<std::uint8_t, 32>;
    Sha256Digest sha256(std::span<const std::uint8_t> input);
    std::string hex_sha256(const Sha256Digest& digest);
} // namespace el9950
