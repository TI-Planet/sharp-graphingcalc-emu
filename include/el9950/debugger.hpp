#pragma once

#include <cstdint>
#include <string>

namespace el9950
{
    class Bus;

    struct DisassemblyLine
    {
        std::uint32_t address{};
        std::uint8_t length{};
        std::string text;
    };

    DisassemblyLine disassemble(const Bus& bus, std::uint32_t address);
} // namespace el9950
