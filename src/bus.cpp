#include "el9950/bus.hpp"
#include "el9950/constants.hpp"

#include <algorithm>

namespace el9950
{
    Bus::Bus(Flash& flash, Lcd& lcd, Tmp91c025& mcu)
    : flash_(flash), lcd_(lcd), mcu_(mcu), ram_(kRamSize, 0) {}

    void Bus::reset() {
        std::fill(ram_.begin(), ram_.end(), 0);
        mcu_.reset();
        lcd_.reset();
    }

    std::uint8_t Bus::read8(std::uint32_t address) {
        address &= kAddressMask;
        if (address < kInternalIoSize) {
            const auto value = mcu_.read(static_cast<std::uint16_t>(address));
            observe_io(false, address, value);
            return value;
        }
        if (address == kLcdCommand) {
            observe_io(false, address, 0);
            return 0;
        }
        if (address == kLcdData) {
            const auto value = lcd_.read_data();
            observe_io(false, address, value);
            return value;
        }
        if (address >= kRamBase && address < kRamBase + ram_.size())
            return ram_[address - kRamBase];
        if (address >= kFlashBase)
            return flash_.read(address - kFlashBase);
        observe_io(false, address, 0xff);
        return 0xff;
    }

    void Bus::write8(std::uint32_t address, std::uint8_t value) {
        address &= kAddressMask;
        if (address < kInternalIoSize) {
            observe_io(true, address, value);
            mcu_.write(static_cast<std::uint16_t>(address), value);
            return;
        }
        if (address == kLcdCommand) {
            observe_io(true, address, value);
            lcd_.command(value);
            return;
        }
        if (address == kLcdData) {
            observe_io(true, address, value);
            lcd_.data(value);
            return;
        }
        if (address >= kRamBase && address < kRamBase + ram_.size()) {
            ram_[address - kRamBase] = value;
            return;
        }
        if (address >= kFlashBase) {
            flash_.write(address - kFlashBase, value);
            return;
        }
        observe_io(true, address, value);
    }

    void Bus::cpu_cycles(std::uint32_t cycles) { mcu_.tick(cycles); }

    bool Bus::interrupt_pending(std::uint8_t source) const {
        return mcu_.interrupt_pending(source);
    }

    bool Bus::any_interrupt_pending() const { return mcu_.any_interrupt_pending(); }

    void Bus::acknowledge_interrupt(std::uint8_t source) {
        mcu_.acknowledge_interrupt(source);
    }

    std::uint8_t Bus::peek8(std::uint32_t address) const {
        address &= kAddressMask;
        if (address >= kRamBase && address < kRamBase + ram_.size())
            return ram_[address - kRamBase];
        if (address >= kFlashBase)
            return flash_.read(address - kFlashBase);
        if (address == kLcdData)
            return lcd_.read_data();
        return 0xff;
    }

    void Bus::observe_io(bool write, std::uint32_t address, std::uint8_t value) {
        if (io_observer_)
            io_observer_(mcu_.timer_ticks(), write, address, value);
    }
} // namespace el9950
