#pragma once

#include "el9950/keyboard.hpp"
#include "el9950/link.hpp"
#include "el9950/constants.hpp"

#include <array>
#include <cstdint>

namespace el9950
{
    class Tmp91c025
    {
    public:
        Tmp91c025(Keyboard& keyboard, LinkPort& link)
        : keyboard_(keyboard), link_(link) {}

        void reset();
        std::uint8_t read(std::uint16_t reg);
        void write(std::uint16_t reg, std::uint8_t value);
        void tick(std::uint32_t cycles);
        void set_operating_battery_percent(std::uint8_t percent);

        std::uint8_t
        operating_battery_percent() const { return operating_battery_percent_; }

        void
        set_backup_battery_healthy(bool healthy) { backup_battery_healthy_ = healthy; }

        bool
        backup_battery_healthy() const { return backup_battery_healthy_; }

        void request_interrupt(std::uint8_t source);
        bool interrupt_pending(std::uint8_t source) const;

        bool
        any_interrupt_pending() const { return pending_interrupts_ != 0; }

        void acknowledge_interrupt(std::uint8_t source);

        std::uint64_t
        pending_interrupts() const { return pending_interrupts_; }

        void
        restore_pending_interrupts(std::uint64_t pending) { pending_interrupts_ = pending; }

        const std::array<std::uint8_t, kInternalIoSize>&
        registers() const { return regs_; }

        void
        restore_registers(const std::array<std::uint8_t, kInternalIoSize>& regs) { regs_ = regs; }

        std::uint64_t
        timer_ticks() const { return timer_ticks_; }

        void
        restore_timer_ticks(std::uint64_t ticks) { timer_ticks_ = ticks; }

        const std::array<std::uint32_t, 6>&
        timer_accumulators() const { return timer_accumulator_; }

        void
        restore_timer_accumulators(const std::array<std::uint32_t, 6>& values) { timer_accumulator_ = values; }

    private:
        Keyboard& keyboard_;
        LinkPort& link_;
        std::array<std::uint8_t, kInternalIoSize> regs_{};
        std::array<std::uint32_t, 6> timer_accumulator_{};
        std::uint64_t timer_ticks_{};
        std::uint64_t pending_interrupts_{};
        std::uint8_t operating_battery_percent_{100};
        bool backup_battery_healthy_{true};
    };
} // namespace el9950
