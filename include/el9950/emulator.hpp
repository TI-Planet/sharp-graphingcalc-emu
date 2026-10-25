#pragma once

#include "el9950/bus.hpp"
#include "el9950/cpu.hpp"
#include "el9950/keyboard.hpp"
#include "el9950/link.hpp"
#include "el9950/rom.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace el9950
{
    class Emulator
    {
    public:
        Emulator();
        void load_rom(const std::filesystem::path& path);
        void load_rom(std::vector<std::uint8_t> bytes);

        bool
        has_rom() const { return rom_.has_value(); }

        const RomImage& rom() const;
        void reset();
        void set_reset_contact(bool open);

        bool
        reset_contact_open() const { return reset_contact_open_; }

        std::uint64_t run_cycles(std::uint64_t cycles);
        void set_key(Key key, bool pressed);
        void set_keyboard_side(KeyboardSide side);
        KeyboardSide keyboard_side() const { return keyboard_.side(); }

        void
        set_operating_battery_percent(std::uint8_t percent) { mcu_.set_operating_battery_percent(percent); }

        std::uint8_t
        operating_battery_percent() const { return mcu_.operating_battery_percent(); }

        void
        set_backup_battery_healthy(bool healthy) { mcu_.set_backup_battery_healthy(healthy); }

        bool
        backup_battery_healthy() const { return mcu_.backup_battery_healthy(); }

        void
        set_irq_line(std::uint8_t line, bool asserted) { cpu_->set_irq(line, asserted); }

        void
        pulse_irq(std::uint8_t line) {
            cpu_->set_irq(line, true);
            cpu_->set_irq(line, false);
        }

        const auto&
        framebuffer() const { return lcd_.framebuffer(); }

        const Keyboard&
        keyboard() const { return keyboard_; }

        Keyboard&
        keyboard() { return keyboard_; }

        LinkPort&
        link() { return link_; }

        const Lcd&
        lcd() const { return lcd_; }

        const Bus&
        bus() const { return bus_; }

        Bus&
        bus() { return bus_; }

        void
        set_io_observer(Bus::IoObserver observer) { bus_.set_io_observer(std::move(observer)); }

        CpuState
        cpu_state() const { return cpu_->state(); }

        std::uint64_t
        cycles() const { return cycles_; }

        bool
        known_rom() const { return rom_ && rom_->is_known_120(); }

        std::vector<std::uint8_t> save_state() const;
        void load_state(std::span<const std::uint8_t> bytes);

        const std::vector<std::uint8_t>&
        last_boot_checkpoint() const { return last_boot_checkpoint_; }

        void
        add_breakpoint(std::uint32_t address) { breakpoints_.insert(address & 0x00ff'ffff); }

        void
        remove_breakpoint(std::uint32_t address) { breakpoints_.erase(address & 0x00ff'ffff); }

        bool
        has_breakpoint(std::uint32_t address) const { return breakpoints_.contains(address & 0x00ff'ffff); }

        const std::unordered_set<std::uint32_t>&
        breakpoints() const { return breakpoints_; }

        bool
        breakpoint_hit() const { return breakpoint_hit_; }

        void
        clear_breakpoint_hit() { breakpoint_hit_ = false; }

    private:
        void install_rom(RomImage rom);
        Keyboard keyboard_;
        LinkPort link_;
        Lcd lcd_;
        Flash flash_;
        Tmp91c025 mcu_;
        Bus bus_;
        std::unique_ptr<Cpu> cpu_;
        std::optional<RomImage> rom_;
        std::uint64_t cycles_{};
        bool reset_contact_open_{};
        mutable bool creating_checkpoint_{};
        std::vector<std::uint8_t> last_boot_checkpoint_;
        std::unordered_set<std::uint32_t> breakpoints_;
        bool breakpoint_hit_{};
    };
} // namespace el9950
