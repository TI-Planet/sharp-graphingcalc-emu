#include "el9950/debugger.hpp"
#include "el9950/bus.hpp"

#include "emu.h"
#include "dasm900.h"

#include <array>
#include <sstream>

namespace el9950
{
    DisassemblyLine disassemble(const Bus& bus, std::uint32_t address) {
        std::array<std::uint8_t, 16> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = bus.peek8(address + static_cast<std::uint32_t>(i));
        const util::disasm_interface::data_buffer buffer(bytes.data(), bytes.size(), address);
        tlcs900_disassembler disassembler;
        std::ostringstream text;
        const auto result = disassembler.disassemble(text, address, buffer, buffer);
        return {address, static_cast<std::uint8_t>(result & util::disasm_interface::LENGTHMASK), text.str()};
    }
} // namespace el9950
