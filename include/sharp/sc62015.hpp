#pragma once

#include "el9950/debugger.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace sharp
{
namespace pock
{
class Cpu;
}

// SC62015 internal memory is a separate address space, never memory-mapped
// onto the external bus. Execution budgets count CPU states (three clocks).
struct Sc62015State
{
    std::uint32_t pc{}, x{}, y{}, u{}, s{};
    std::uint16_t ba{}, i{};
    std::uint8_t flags{};
    std::array<std::uint8_t, 256> internal{};
    std::uint64_t main_timer{}, sub_timer{};
    bool halted{}, off{};
};

class Sc62015Bus
{
  public:
    virtual ~Sc62015Bus() = default;
    virtual std::uint8_t read8(std::uint32_t address) = 0;
    virtual std::uint8_t peek8(std::uint32_t address) const = 0;
    virtual void write8(std::uint32_t address, std::uint8_t value) = 0;
    virtual std::uint8_t input(std::uint8_t port, std::uint8_t latch) const
    {
        (void)port;
        return latch;
    }
    virtual void output(std::uint8_t port, std::uint8_t value)
    {
        (void)port;
        (void)value;
    }
};

class Sc62015
{
  public:
    explicit Sc62015(Sc62015Bus &bus);
    ~Sc62015();
    void reset();
    std::uint32_t step();
    Sc62015State state() const;
    void set_state(const Sc62015State &state);
    void request_interrupt(std::uint8_t sources);
    std::uint8_t read_internal(std::uint8_t address) const;
    void write_internal(std::uint8_t address, std::uint8_t value);
    el9950::DisassemblyLine disassemble(std::uint32_t address) const;
    static constexpr std::uint32_t address_mask = 0xfffff;
    static constexpr std::uint32_t states_per_second = 768000; // 2.304 MHz / 3; board clock provisional

  private:
    Sc62015Bus &bus_;
    std::unique_ptr<pock::Cpu> core_;
};

} // namespace sharp
