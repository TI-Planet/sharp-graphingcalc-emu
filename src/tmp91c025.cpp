#include "el9950/tmp91c025.hpp"

#include <algorithm>

namespace el9950
{
    namespace
    {
        // These register aliases isolate the currently implemented EL-9950 wiring so
        // it can be refined without changing the Bus/Keyboard API.
        constexpr std::uint8_t kKeyboardDriveLow = 0x13;
        constexpr std::uint8_t kKeyboardDriveHigh = 0x06;
        constexpr std::uint8_t kKeyboardInputs = 0x19;
        constexpr std::uint8_t kPortOn = 0x22;
        constexpr std::uint8_t kOnMask = 0x10;
        constexpr std::uint8_t kBackupBatteryMask = 0x02;
        constexpr std::uint8_t kBatterySensePort = 0x29;
        constexpr std::uint8_t kOperatingBatteryLowMask = 0x40;
        constexpr std::uint16_t kAlarmEnable = 0x0334;

        // Model the five wall-clock alarms as 1, 2, 16, 500 and 1000 ms periods.
        // Convert those periods to deterministic TMP91C025 cycles; alarm 16 is the keyboard
        // debounce/repeat tick used by ROM ISR F3D574.
        constexpr std::array<std::uint32_t, 5> kAlarmPeriods{
            kClockHz / 1000,
            kClockHz * 2 / 1000,
            kClockHz * 16 / 1000,
            kClockHz * 500 / 1000,
            kClockHz,
        };

        std::uint8_t keyboard_outputs(const std::array<std::uint8_t, kInternalIoSize>& regs)
        {
            // Rows 0..3 are P6 bits 7..4; rows 4..6 are P1 bits 3..1. All are
            // active-low. This is the exact seven-step drive sequence emitted by ROM
            // routine F37086 before each read from P8 (0x19).
            std::uint8_t result = 0;
            for (std::uint8_t row = 0; row < 4; ++row)
                result |= ((regs[kKeyboardDriveHigh] >> (7 - row)) & 1U) << row;
            for (std::uint8_t row = 4; row < 7; ++row)
                result |= ((regs[kKeyboardDriveLow] >> (7 - row)) & 1U) << row;
            return result;
        }
    }

    void Tmp91c025::reset()
    {
        regs_.fill(0);
        timer_accumulator_.fill(0);
        timer_ticks_ = 0;
        pending_interrupts_ = 0;
        regs_[kKeyboardInputs] = 0xff;
        // P2.3 is high on the calculator at reset. Leaving it low sends the boot
        // firmware down its battery-contact shutdown path at FFEC49 instead of the
        // normal idle/power-on path at FFA0A9. P2.1 is the backup-battery sense
        // input; it is overlaid in read() because firmware writes to the same port.
        regs_[kPortOn] = 0x08;
        for (std::uint8_t line = 0; line < 3; ++line)
            link_.drive(line, true);
    }

    std::uint8_t Tmp91c025::read(std::uint16_t reg)
    {
        if (reg == kKeyboardInputs)
            return keyboard_.read_inputs(keyboard_outputs(regs_));
        if (reg == kPortOn)
        {
            auto value = static_cast<std::uint8_t>(regs_[reg] & ~(kOnMask | kBackupBatteryMask));
            // P2.4 is pulled high while ON is released and grounded by the key.
            // ROM routine F376DA polls this active-low level as its BREAK check
            // during calculations and graph evaluation.
            if (!keyboard_.on_pressed())
                value |= kOnMask;
            if (backup_battery_healthy_)
                value |= kBackupBatteryMask;
            return value;
        }
        if (reg == kBatterySensePort)
        {
            auto value = static_cast<std::uint8_t>(regs_[reg] & ~kOperatingBatteryLowMask);
            // The firmware drives P4.7, waits for the comparator to settle, and reads
            // P4.6. The analogue circuit only exposes a low-battery threshold to the
            // MCU; the percentage is retained so frontends and test harnesses can use
            // a natural control while the guest sees the real binary signal.
            if (operating_battery_percent_ <= 10)
                value |= kOperatingBatteryLowMask;
            return value;
        }
        return regs_[reg];
    }

    void Tmp91c025::write(std::uint16_t reg, std::uint8_t value)
    {
        if (reg == kAlarmEnable)
        {
            const auto changed = static_cast<std::uint8_t>(regs_[reg] ^ value);
            for (std::size_t alarm = 0; alarm < kAlarmPeriods.size(); ++alarm)
                if (changed & (1U << alarm))
                    timer_accumulator_[alarm] = 0;
        }
        regs_[reg] = value;
    }

    void Tmp91c025::set_operating_battery_percent(std::uint8_t percent)
    {
        operating_battery_percent_ = std::min<std::uint8_t>(percent, 100);
    }

    void Tmp91c025::request_interrupt(std::uint8_t source)
    {
        if (source < 64)
            pending_interrupts_ |= std::uint64_t{1} << source;
    }

    bool Tmp91c025::interrupt_pending(std::uint8_t source) const
    {
        return source < 64 && ((pending_interrupts_ >> source) & 1U);
    }

    void Tmp91c025::acknowledge_interrupt(std::uint8_t source)
    {
        if (source < 64)
            pending_interrupts_ &= ~(std::uint64_t{1} << source);
    }

    void Tmp91c025::tick(std::uint32_t cycles)
    {
        timer_ticks_ += cycles;
        // Firmware routine F3766F starts the operation with SET 2,(0xE0), then
        // polls that same bit until the peripheral clears it. At least one complete
        // CPU instruction elapses before tick(), so acknowledge it here.
        if (cycles)
            regs_[0x00e0] &= static_cast<std::uint8_t>(~0x04U);
        // Alarm 2 / source 16 drives the key debounce path. Other alarm sources
        // remain gated until their peripheral behavior is implemented.
        constexpr std::size_t alarm = 2;
        if (!(regs_[kAlarmEnable] & (1U << alarm)))
            timer_accumulator_[alarm] = 0;
        else
        {
            auto& accumulator = timer_accumulator_[alarm];
            accumulator += cycles;
            if (accumulator >= kAlarmPeriods[alarm])
            {
                accumulator %= kAlarmPeriods[alarm];
                request_interrupt(16);
            }
        }
    }
} // namespace el9950
