#pragma once

#include "el9950/cpu.hpp"
#include "el9950/flash.hpp"
#include "el9950/lcd.hpp"
#include "el9950/tmp91c025.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace el9950
{
    class Bus final : public CpuBus
    {
    public:
        using IoObserver = std::function<void(
            std::uint64_t cycle, bool write,
            std::uint32_t address, std::uint8_t value)>;

        Bus(Flash& flash, Lcd& lcd, Tmp91c025& mcu);
        void reset();
        std::uint8_t read8(std::uint32_t address) override;
        void write8(std::uint32_t address, std::uint8_t value) override;
        void cpu_cycles(std::uint32_t cycles) override;
        bool interrupt_pending(std::uint8_t source) const override;
        bool any_interrupt_pending() const override;
        void acknowledge_interrupt(std::uint8_t source) override;

        std::span<std::uint8_t>
        ram() { return ram_; }

        std::span<const std::uint8_t>
        ram() const { return ram_; }

        std::uint8_t peek8(std::uint32_t address) const;

        void
        set_io_observer(IoObserver observer) { io_observer_ = std::move(observer); }

    private:
        void observe_io(bool write, std::uint32_t address, std::uint8_t value);
        Flash& flash_;
        Lcd& lcd_;
        Tmp91c025& mcu_;
        std::vector<std::uint8_t> ram_;
        IoObserver io_observer_;
    };
} // namespace el9950
