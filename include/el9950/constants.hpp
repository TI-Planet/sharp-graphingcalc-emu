#pragma once

#include <cstddef>
#include <cstdint>

namespace el9950
{
    inline constexpr std::uint32_t kAddressMask = 0x00ff'ffff;
    inline constexpr std::size_t kInternalIoSize = 0x800;
    inline constexpr std::uint32_t kFlashBase = 0x00f0'0000;
    inline constexpr std::size_t kFlashSize = 0x10'0000;
    inline constexpr std::uint32_t kBootPageBase = 0x00ff'c000;
    inline constexpr std::size_t kBootPageSize = 0x4000;
    inline constexpr std::uint32_t kRamBase = 0x0040'0000;
    inline constexpr std::size_t kRamSize = 0x4'0000;
    inline constexpr std::uint32_t kLcdCommand = 0x0001'0000;
    inline constexpr std::uint32_t kLcdData = 0x0001'0001;
    inline constexpr std::size_t kLcdWidth = 132;
    inline constexpr std::size_t kLcdHeight = 64;
    inline constexpr std::uint32_t kClockHz = 9'830'400;
    inline constexpr char kKnownRom120Sha256[] =
        "deae0913c635b02e5b273db20905fa57993ec035edb7cbf78f08f9bc79f42082";
} // namespace el9950
