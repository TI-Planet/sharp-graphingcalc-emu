#include "sharp/sc62015.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace sharp
{
namespace
{
constexpr std::uint32_t mask20 = Sc62015::address_mask;
constexpr unsigned widths[]{1, 1, 2, 2, 3, 3, 3, 3, 1, 1};
constexpr const char *names[]{"A", "IL", "BA", "I", "X", "Y", "U", "S", "F", "IMR", "B"};
enum class Kind
{
    none,
    immediate,
    reg,
    internal,
    external,
    indexed,
    indirect
};
enum class Op
{
    invalid,
    nop,
    mv,
    exchange,
    add,
    adc,
    sub,
    sbc,
    compare,
    test,
    bit_and,
    bit_or,
    bit_xor,
    increment,
    decrement,
    jump,
    jump_far,
    relative,
    call,
    call_far,
    ret,
    ret_far,
    push,
    pop,
    interrupt,
    reti,
    set_carry,
    clear_carry,
    rotate_left,
    rotate_right,
    shift_left,
    shift_right,
    swap,
    halt,
    off,
    wait,
    reset,
    timer_clear,
    block_add,
    block_sub,
    decimal_add,
    decimal_sub,
    digit_left,
    digit_right,
    pmdf
};
struct Operand
{
    Kind kind{Kind::none};
    std::uint32_t value{};
    unsigned mode{};
    int offset{};
    int update{}; // -1 pre-decrement, +1 post-increment
    bool second{};
};
struct Instruction
{
    Op op{Op::invalid};
    Operand dst{}, src{};
    unsigned size{1}, states{1}, length{};
    unsigned condition{}; // 0 always, 1 Z, 2 NZ, 3 C, 4 NC
    std::uint32_t next{};
    std::uint8_t opcode{};
    bool block{}, descending{}, user_stack{true};
};
Operand r(unsigned index) { return {Kind::reg, index}; }
Operand imm(std::uint32_t value) { return {Kind::immediate, value}; }
std::string hex(std::uint32_t value, unsigned width = 2)
{
    std::ostringstream out;
    out << '$' << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << value;
    return out.str();
}

// Decode once for execution and disassembly. Fetch wraps within a 64 KiB
// program segment; data pointers wrap across the entire 20-bit address space.
Instruction decode(const std::function<std::uint8_t(std::uint32_t)> &read, std::uint32_t address)
{
    Instruction d;
    auto pc = address & mask20;
    unsigned modes[]{1, 1}; // BP-relative unless overridden by a pre-byte
    auto byte = [&]()
    {
        const auto value = read(pc);
        pc = (pc & 0xf0000) | ((pc + 1) & 0xffff);
        ++d.length;
        return value;
    };
    auto number = [&](unsigned size)
    {
        std::uint32_t value = 0;
        for (unsigned n = 0; n < size; ++n)
            value |= std::uint32_t(byte()) << (8 * n);
        return value;
    };
    auto mem = [&](bool second = false)
    { return Operand{Kind::internal, byte(), modes[second ? 1 : 0], 0, 0, second}; };
    auto absolute = [&]() { return Operand{Kind::external, number(3) & mask20}; };
    auto indexed = [&](std::uint8_t post)
    {
        Operand o{Kind::indexed, post & 7U};
        if (o.value < 4 || (post & 0x08))
            return Operand{};
        if (post & 0x80)
        {
            o.offset = byte();
            if (post & 0x40)
                o.offset = -o.offset;
            return o;
        }
        switch (post & 0x30)
        {
        case 0x10:
            return Operand{};
        case 0x20:
            o.update = 1;
            break;
        case 0x30:
            o.update = -1;
            break;
        default:
            break;
        }
        return o;
    };
    // Bit 7 denotes an offset in indirect mode; bit 6 is its sign.
    auto indirect = [&](std::uint8_t post, Operand base)
    {
        base.kind = Kind::indirect;
        if (post & 0x80)
        {
            base.offset = byte();
            if (post & 0x40)
                base.offset = -base.offset;
        }
        if (post & 0x3f)
            base.kind = Kind::none;
        return base;
    };
    auto op = byte();
    while ((op >= 0x21 && op <= 0x27) || (op >= 0x30 && op <= 0x37))
    {
        if (d.length > 16)
        {
            d.next = pc;
            return d;
        }
        modes[0] = ((op & 0x10) ? 0U : 1U) + ((op & 4) ? 2U : 0U);
        constexpr unsigned second_modes[]{1, 3, 0, 2};
        modes[1] = second_modes[op & 3];
        ++d.states;
        op = byte();
    }
    d.opcode = op;
    const auto group = op >> 4, low = op & 15;
    if (op >= 0x08 && op <= 0x0f)
    {
        d.op = Op::mv;
        d.dst = r(low - 8);
        d.size = widths[low - 8];
        d.src = imm(number(d.size));
        d.states += 1 + d.size;
    }
    else if ((op >= 0x28 && op <= 0x2f) || (op >= 0x38 && op <= 0x3f))
    {
        const auto index = low - 8;
        d.op = group == 2 ? Op::push : Op::pop;
        d.dst = r(index < 6 ? index : index + 2);
        d.size = widths[d.dst.value];
        d.states += 2 + d.size;
    }
    else if (op >= 0x12 && op <= 0x1f)
    {
        if (op == 0x12 || op == 0x13)
        {
            d.op = Op::relative;
            const auto offset = byte();
            d.src = imm((op & 1) ? std::uint32_t(-int(offset)) : offset);
            d.states += 2;
        }
        else if (low <= 7)
        {
            d.op = Op::jump;
            d.src = imm(number(2));
            d.condition = low - 3;
            d.states += 3;
        }
        else
        {
            d.op = Op::relative;
            const auto offset = byte();
            d.src = imm((low & 1) ? std::uint32_t(-int(offset)) : offset);
            d.condition = (low - 6) / 2;
            d.states += 2;
        }
    }
    else if (group == 8 || group == 0xa)
    {
        const auto index = low & 7;
        d.op = Op::mv;
        d.size = widths[index];
        const auto memory = (low & 8) ? absolute() : mem();
        d.dst = group == 8 ? r(index) : memory;
        d.src = group == 8 ? memory : r(index);
        d.states += ((low & 8) ? 4U : 1U) + d.size;
    }
    else if ((group == 9 || group == 0xb) && low != 7 && low != 15)
    {
        d.op = Op::mv;
        d.size = widths[low & 7];
        const auto post = byte();
        Operand memory;
        if (low & 8)
            memory = indirect(post, mem());
        else
            memory = indexed(post);
        d.dst = group == 9 ? r(low & 7) : memory;
        d.src = group == 9 ? memory : r(low & 7);
        d.states += (low & 8 ? 8U : 2U) + d.size;
        if (memory.kind == Kind::none)
            d.op = Op::invalid;
    }
    else if ((op >= 0xc8 && op <= 0xcb) || op == 0xcf)
    {
        d.op = Op::mv;
        d.dst = mem();
        d.src = mem(true);
        d.size = (op & 3) + 1;
        d.block = op == 0xcb || op == 0xcf;
        d.descending = op == 0xcf;
        d.states += 3 + 2 * d.size;
    }
    else if (op >= 0xd0 && low <= 3)
    {
        d.op = Op::mv;
        d.size = low + 1;
        d.block = low == 3;
        if (group == 0xd)
        {
            d.dst = mem();
            d.src = absolute();
        }
        else
        {
            const auto post = byte();
            d.dst = mem();
            d.src = group == 0xe ? indexed(post) : indirect(post, mem(true));
            if (d.src.kind == Kind::none)
                d.op = Op::invalid;
        }
        d.states += (group == 0xf ? 9 : 4) + d.size;
    }
    else if (op >= 0xd8 && low >= 8 && low <= 11)
    {
        d.op = Op::mv;
        d.size = low - 7;
        d.block = low == 11;
        if (group == 0xd)
        {
            d.dst = absolute();
            d.src = mem();
        }
        else
        {
            const auto post = byte();
            if (group == 0xe)
            {
                // The displacement follows the internal operand byte.
                const auto source = mem();
                d.dst = indexed(post);
                d.src = source;
            }
            else
            {
                const auto base = mem();
                d.src = mem(true);
                d.dst = indirect(post, base);
            }
            if (d.dst.kind == Kind::none)
                d.op = Op::invalid;
        }
        d.states += (group == 0xf ? 9 : 4) + d.size;
    }
    else if ((op >= 0x40 && op <= 0x43) || (op >= 0x48 && op <= 0x4b) || (op >= 0x50 && op <= 0x53) ||
             (op >= 0x58 && op <= 0x5b) || (op >= 0x60 && op <= 0x6b) || (op >= 0x70 && op <= 0x73) ||
             (op >= 0x78 && op <= 0x7b))
    {
        if (group == 4)
            d.op = (low & 8) ? Op::sub : Op::add;
        if (group == 5)
            d.op = (low & 8) ? Op::sbc : Op::adc;
        if (group == 6)
            d.op = low < 4 ? Op::compare : low < 8 ? Op::test : Op::bit_xor;
        if (group == 7)
            d.op = (low & 8) ? Op::bit_or : Op::bit_and;
        switch (low & 3)
        {
        case 0:
            d.dst = r(0);
            d.src = imm(byte());
            break;
        case 1:
            d.dst = mem();
            d.src = imm(byte());
            break;
        case 2:
            if (group <= 5)
            {
                d.dst = r(0);
                d.src = mem();
            }
            else
            {
                d.dst = absolute();
                d.src = imm(byte());
            }
            break;
        case 3:
            d.dst = mem();
            d.src = r(0);
            break;
        }
        d.states += 1 + (d.dst.kind == Kind::internal || d.src.kind == Kind::internal ? 1U : 0U) +
                    (d.dst.kind == Kind::external ? 4U : 0U);
    }
    else
    {
        switch (op)
        {
        case 0x00:
            d.op = Op::nop;
            break;
        case 0x01:
            d.op = Op::reti;
            d.states += 6;
            break;
        case 0x02:
            d.op = Op::jump;
            d.src = imm(number(2));
            d.states += 2;
            break;
        case 0x03:
            d.op = Op::jump_far;
            d.src = imm(number(3));
            d.states += 3;
            break;
        case 0x04:
            d.op = Op::call;
            d.src = imm(number(2));
            d.states += 5;
            break;
        case 0x05:
            d.op = Op::call_far;
            d.src = imm(number(3));
            d.states += 7;
            break;
        case 0x06:
            d.op = Op::ret;
            d.states += 3;
            break;
        case 0x07:
            d.op = Op::ret_far;
            d.states += 4;
            break;
        case 0x10:
            d.op = Op::jump_far;
            d.src = mem();
            d.size = 3;
            d.states += 5;
            break;
        case 0x11:
            d.op = Op::jump_far;
            d.src = r(byte() & 7);
            d.size = 3;
            d.states += 2;
            break;
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0xed:
        case 0xfd:
        {
            const auto post = byte();
            d.dst = r((post >> 4) & 7);
            d.src = r(post & 7);
            d.size = widths[d.dst.value];
            d.op = op == 0xfd ? Op::mv : op == 0xed ? Op::exchange : (op & 8) ? Op::sub : Op::add;
            if (op == 0xfd || op == 0xed)
            {
                if (d.dst.value < 2)
                    d.dst.value += 2;
                if (d.src.value < 2)
                    d.src.value += 2;
                d.size = widths[d.dst.value];
            }
            d.states += 1;
            break;
        }
        case 0x47:
        case 0x57:
            d.op = Op::pmdf;
            d.dst = mem();
            d.src = op == 0x47 ? imm(byte()) : r(0);
            break;
        case 0x4f:
        case 0x5f:
            d.op = op == 0x4f ? Op::push : Op::pop;
            d.dst = r(8);
            d.user_stack = false;
            d.states += 2;
            break;
        case 0x54:
        case 0x55:
        case 0x5c:
        case 0x5d:
        case 0xc4:
        case 0xc5:
        case 0xd4:
        case 0xd5:
            d.op = group == 5     ? ((op & 8) ? Op::block_sub : Op::block_add)
                   : group == 0xc ? Op::decimal_add
                                  : Op::decimal_sub;
            d.dst = mem();
            d.src = (op & 1) ? r(0) : mem(true);
            d.block = true;
            break;
        case 0x56:
        case 0x5e:
        {
            const auto memory = mem();
            Operand ext{Kind::indexed, 4, 0, int(byte())};
            d.op = Op::mv;
            d.block = true;
            d.dst = op == 0x56 ? memory : ext;
            d.src = op == 0x56 ? ext : memory;
            break;
        }
        case 0x6c:
        case 0x7c:
            d.op = op == 0x6c ? Op::increment : Op::decrement;
            d.dst = r(byte() & 7);
            d.size = widths[d.dst.value];
            d.states += 1;
            break;
        case 0x6d:
        case 0x7d:
            d.op = op == 0x6d ? Op::increment : Op::decrement;
            d.dst = mem();
            d.states += 2;
            break;
        case 0x6e:
        case 0x76:
        case 0x7e:
            d.op = op == 0x6e ? Op::bit_xor : op == 0x76 ? Op::bit_and : Op::bit_or;
            d.dst = mem();
            d.src = mem(true);
            d.states += 4;
            break;
        case 0x6f:
        case 0x77:
        case 0x7f:
            d.op = op == 0x6f ? Op::bit_xor : op == 0x77 ? Op::bit_and : Op::bit_or;
            d.dst = r(0);
            d.src = mem();
            d.states += 2;
            break;
        case 0x74:
        case 0x75:
            d.op = Op::mv;
            d.dst = r(op == 0x74 ? 0 : 10);
            d.src = r(op == 0x74 ? 10 : 0);
            d.size = 1;
            break;
        case 0xb7:
            d.op = Op::compare;
            d.dst = mem();
            d.src = mem(true);
            break;
        case 0xd6:
        case 0xd7:
        {
            const auto source = r(byte() & 7);
            d.op = Op::compare;
            d.dst = mem();
            d.src = source;
            d.size = op == 0xd6 ? 2 : 3;
            break;
        }
        case 0x97:
            d.op = Op::set_carry;
            break;
        case 0x9f:
            d.op = Op::clear_carry;
            break;
        case 0xc0:
        case 0xc1:
        case 0xc2:
        case 0xc3:
            d.op = Op::exchange;
            d.dst = mem();
            d.src = mem(true);
            d.size = low + 1;
            d.block = low == 3;
            d.states += 2 * d.size;
            break;
        case 0xc6:
        case 0xc7:
            d.op = Op::compare;
            d.dst = mem();
            d.src = mem(true);
            d.size = low - 4;
            d.states += 2 * d.size;
            break;
        case 0xcc:
        case 0xcd:
        case 0xdc:
            d.op = Op::mv;
            d.size = op == 0xcc ? 1 : op == 0xcd ? 2 : 3;
            d.dst = mem();
            d.src = imm(number(d.size));
            d.states += 1 + d.size;
            break;
        case 0xce:
            d.op = Op::timer_clear;
            break;
        case 0xde:
            d.op = Op::halt;
            d.states += 1;
            break;
        case 0xdf:
            d.op = Op::off;
            d.states += 1;
            break;
        case 0xe4:
        case 0xe5:
        case 0xe6:
        case 0xe7:
        case 0xf4:
        case 0xf5:
        case 0xf6:
        case 0xf7:
            d.op = group == 0xe ? ((op & 2) ? Op::rotate_left : Op::rotate_right)
                                : ((op & 2) ? Op::shift_left : Op::shift_right);
            d.dst = (op & 1) ? mem() : r(0);
            d.states += (op & 1) ? 2 : 0;
            break;
        case 0xec:
        case 0xfc:
            d.op = op == 0xec ? Op::digit_left : Op::digit_right;
            d.dst = mem();
            d.block = true;
            break;
        case 0xdd:
            d.op = Op::exchange;
            d.dst = r(0);
            d.src = r(10);
            d.size = 1;
            break;
        case 0xee:
            d.op = Op::swap;
            d.dst = r(0);
            break;
        case 0xef:
            d.op = Op::wait;
            break;
        case 0xfe:
            d.op = Op::interrupt;
            d.states += 11;
            break;
        case 0xff:
            d.op = Op::reset;
            d.states += 5;
            break;
        default:
            break;
        }
    }
    d.next = pc;
    return d;
}

std::string operand_text(const Operand &o)
{
    auto internal = [&]()
    {
        const std::string offset = hex(o.value);
        switch (o.mode)
        {
        case 0:
            return "(" + offset + ")";
        case 1:
            return "(BP+" + offset + ")";
        case 2:
            return std::string(o.second ? "(PY+" : "(PX+") + offset + ")";
        default:
            return std::string(o.second ? "(BP+PY)" : "(BP+PX)");
        }
    };
    switch (o.kind)
    {
    case Kind::none:
        return {};
    case Kind::reg:
        return names[o.value];
    case Kind::immediate:
        return hex(o.value, o.value > 0xffff ? 5 : o.value > 0xff ? 4 : 2);
    case Kind::internal:
        return internal();
    case Kind::external:
        return "[" + hex(o.value, 5) + "]";
    case Kind::indirect:
        return "[" + internal() + (o.offset ? (o.offset < 0 ? "-" : "+") + hex(unsigned(std::abs(o.offset))) : "") +
               "]";
    case Kind::indexed:
        return std::string("[") + (o.update < 0 ? "--" : "") + names[o.value] + (o.update > 0 ? "++" : "") +
               (o.offset ? (o.offset < 0 ? "-" : "+") + hex(unsigned(std::abs(o.offset))) : "") + "]";
    }
    return {};
}
std::string instruction_text(const Instruction &d)
{
    std::string name;
    switch (d.op)
    {
    case Op::nop:
        name = "NOP";
        break;
    case Op::mv:
        name = d.block ? (d.descending ? "MVLD" : "MVL") : "MV";
        break;
    case Op::exchange:
        name = d.block ? "EXL" : "EX";
        break;
    case Op::add:
        name = "ADD";
        break;
    case Op::adc:
        name = "ADC";
        break;
    case Op::sub:
        name = "SUB";
        break;
    case Op::sbc:
        name = "SBC";
        break;
    case Op::compare:
        name = "CMP";
        break;
    case Op::test:
        name = "TEST";
        break;
    case Op::bit_and:
        name = "AND";
        break;
    case Op::bit_or:
        name = "OR";
        break;
    case Op::bit_xor:
        name = "XOR";
        break;
    case Op::increment:
        name = "INC";
        break;
    case Op::decrement:
        name = "DEC";
        break;
    case Op::jump:
    case Op::jump_far:
        name = d.op == Op::jump ? "JP" : "JPF";
        break;
    case Op::relative:
        name = "JR";
        break;
    case Op::call:
        name = "CALL";
        break;
    case Op::call_far:
        name = "CALLF";
        break;
    case Op::ret:
        name = "RET";
        break;
    case Op::ret_far:
        name = "RETF";
        break;
    case Op::push:
        name = d.user_stack ? "PUSHU" : "PUSHS";
        break;
    case Op::pop:
        name = d.user_stack ? "POPU" : "POPS";
        break;
    case Op::interrupt:
        name = "INT";
        break;
    case Op::reti:
        name = "RETI";
        break;
    case Op::set_carry:
        name = "SC";
        break;
    case Op::clear_carry:
        name = "RC";
        break;
    case Op::rotate_left:
        name = "ROL";
        break;
    case Op::rotate_right:
        name = "ROR";
        break;
    case Op::shift_left:
        name = "SHL";
        break;
    case Op::shift_right:
        name = "SHR";
        break;
    case Op::swap:
        name = "SWAP";
        break;
    case Op::halt:
        name = "HALT";
        break;
    case Op::off:
        name = "OFF";
        break;
    case Op::wait:
        name = "WAIT";
        break;
    case Op::reset:
        name = "RESET";
        break;
    case Op::timer_clear:
        name = "TCL";
        break;
    case Op::block_add:
        name = "ADCL";
        break;
    case Op::block_sub:
        name = "SBCL";
        break;
    case Op::decimal_add:
        name = "DADL";
        break;
    case Op::decimal_sub:
        name = "DSBL";
        break;
    case Op::digit_left:
        name = "DSLL";
        break;
    case Op::digit_right:
        name = "DSRL";
        break;
    case Op::pmdf:
        name = "PMDF";
        break;
    default:
        return "DB " + hex(d.opcode);
    }
    constexpr const char *conditions[]{"", "Z", "NZ", "C", "NC"};
    name += conditions[d.condition];
    auto dst = operand_text(d.dst), src = operand_text(d.src);
    if (d.op == Op::relative)
        src = hex((d.next & 0xf0000) | ((d.next + d.src.value) & 0xffff), 5);
    if (!dst.empty())
        name += " " + dst;
    if (!src.empty())
        name += (dst.empty() ? " " : ",") + src;
    return name;
}
} // namespace

el9950::DisassemblyLine Sc62015::disassemble(std::uint32_t address) const
{
    const auto d = decode([&](auto a) { return bus_.peek8(a); }, address);
    return {address & mask20, static_cast<std::uint8_t>(d.length), instruction_text(d), d.next};
}
} // namespace sharp
