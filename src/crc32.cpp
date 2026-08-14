#include "el9950/state.hpp"

namespace el9950
{
    std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
        std::uint32_t crc = 0xffff'ffffU;
        for (const auto byte : bytes) {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb8'8320U & (0U - (crc & 1U)));
        }
        return ~crc;
    }
} // namespace el9950
