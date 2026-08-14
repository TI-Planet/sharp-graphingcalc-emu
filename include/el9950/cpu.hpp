#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace el9950
{
    class CpuBus
    {
    public:
        virtual ~CpuBus() = default;
        virtual std::uint8_t read8(std::uint32_t address) = 0;
        virtual void write8(std::uint32_t address, std::uint8_t value) = 0;
        virtual void cpu_cycles(std::uint32_t cycles) = 0;
        virtual bool interrupt_pending(std::uint8_t source) const = 0;
        virtual bool any_interrupt_pending() const = 0;
        virtual void acknowledge_interrupt(std::uint8_t source) = 0;
    };

    struct CpuState
    {
        std::uint32_t pc{};
        std::uint32_t sr{};
        std::array<std::uint32_t, 16> xwa_xbc_xde_xhl{};
        std::uint32_t xix{}, xiy{}, xiz{}, xnsp{}, xssp{};
        std::array<std::uint32_t, 16> dma{};
        std::uint8_t register_bank{};
        bool halted{};
    };

    class Cpu
    {
    public:
        virtual ~Cpu() = default;
        virtual void reset() = 0;
        virtual std::uint32_t execute(std::uint32_t cycle_budget) = 0;
        virtual CpuState state() const = 0;
        virtual void set_state(const CpuState& state) = 0;
        virtual void set_irq(std::uint8_t line, bool asserted) = 0;
    };

    std::unique_ptr<Cpu> make_tlcs900_l1_cpu(CpuBus& bus);
} // namespace el9950
