#include "sharp/machine.hpp"
#include "el9950/constants.hpp"
#include "el9950/emulator.hpp"

#include <array>
#include <stdexcept>

namespace sharp
{
namespace
{
class El99Machine final : public Machine
{
  public:
    explicit El99Machine(const el9950::RomImage &image)
    {
        emulator_.load_rom(std::vector<std::uint8_t>(image.bytes().begin(), image.bytes().end()));
    }
    MachineInfo info() const override
    {
        return {"el99xx",
                "TLCS-900/L1",
                el9950::kClockHz,
                el9950::kClockHz,
                el9950::kAddressMask,
                el9950::kRamBase,
                el9950::kFlashBase,
                132,
                64,
                false};
    }
    const el9950::RomImage &rom() const override { return emulator_.rom(); }
    void reset() override { emulator_.reset(); }
    void set_reset_contact(bool open) override { emulator_.set_reset_contact(open); }
    bool reset_contact_open() const override { return emulator_.reset_contact_open(); }
    std::uint64_t run_cycles(std::uint64_t budget) override { return emulator_.run_cycles(budget); }
    std::uint64_t cycles() const override { return emulator_.cycles(); }
    void set_key(el9950::Key key, bool pressed) override { emulator_.set_key(key, pressed); }
    const el9950::Keyboard &keyboard() const override { return emulator_.keyboard(); }
    void set_keyboard_side(el9950::KeyboardSide side) override { emulator_.set_keyboard_side(side); }
    void set_operating_battery_percent(std::uint8_t percent) override
    {
        emulator_.set_operating_battery_percent(percent);
    }
    std::uint8_t operating_battery_percent() const override { return emulator_.operating_battery_percent(); }
    void set_backup_battery_healthy(bool healthy) override { emulator_.set_backup_battery_healthy(healthy); }
    bool backup_battery_healthy() const override { return emulator_.backup_battery_healthy(); }
    void set_irq_line(std::uint8_t line, bool asserted) override { emulator_.set_irq_line(line, asserted); }
    std::span<const std::uint8_t> framebuffer() const override { return emulator_.framebuffer(); }
    CpuState cpu_state() const override
    {
        const auto s = emulator_.cpu_state();
        CpuState result{s.pc, s.sr, s.halted, {}};
        for (unsigned bank = 0; bank < 4; ++bank)
            for (unsigned reg = 0; reg < 4; ++reg)
            {
                constexpr const char *names[]{"XWA", "XBC", "XDE", "XHL"};
                result.registers.push_back(
                    {std::string(names[reg]) + std::to_string(bank), s.xwa_xbc_xde_xhl[bank * 4 + reg], 32});
            }
        result.registers.insert(result.registers.end(), {{"XIX", s.xix, 32},
                                                         {"XIY", s.xiy, 32},
                                                         {"XIZ", s.xiz, 32},
                                                         {"XNSP", s.xnsp, 32},
                                                         {"XSSP", s.xssp, 32},
                                                         {"BANK", s.register_bank, 8}});
        return result;
    }
    std::vector<DebugRegister> device_registers() const override
    {
        const auto &lcd = emulator_.lcd();
        return {{"Page", lcd.page(), 8},
                {"Column", lcd.column(), 8},
                {"Contrast", lcd.contrast(), 8},
                {"Display", std::uint32_t(lcd.display_on()), 1},
                {"Inverted", std::uint32_t(lcd.inverted()), 1},
                {"ADC reverse", std::uint32_t(lcd.adc_reverse()), 1},
                {"COM reverse", std::uint32_t(lcd.com_reverse()), 1}};
    }
    std::uint8_t peek8(std::uint32_t address) const override { return emulator_.bus().peek8(address); }
    void write8(std::uint32_t address, std::uint8_t value) override { emulator_.bus().write8(address, value); }
    void set_io_observer(IoObserver observer) override { emulator_.set_io_observer(std::move(observer)); }
    el9950::DisassemblyLine disassemble(std::uint32_t address) const override
    {
        return el9950::disassemble(emulator_.bus(), address);
    }
    std::vector<std::uint8_t> save_state() const override { return emulator_.save_state(); }
    void load_state(std::span<const std::uint8_t> bytes) override { emulator_.load_state(bytes); }
    void add_breakpoint(std::uint32_t address) override { emulator_.add_breakpoint(address); }
    void remove_breakpoint(std::uint32_t address) override { emulator_.remove_breakpoint(address); }
    const std::unordered_set<std::uint32_t> &breakpoints() const override { return emulator_.breakpoints(); }
    bool breakpoint_hit() const override { return emulator_.breakpoint_hit(); }
    el9950::LinkPort &link() override { return emulator_.link(); }

  private:
    el9950::Emulator emulator_;
};
} // namespace
std::unique_ptr<Machine> make_el99_machine(el9950::RomImage image) { return std::make_unique<El99Machine>(image); }
Machine &Emulator::machine()
{
    if (!machine_)
        throw std::runtime_error("no ROM loaded");
    return *machine_;
}
const Machine &Emulator::machine() const
{
    if (!machine_)
        throw std::runtime_error("no ROM loaded");
    return *machine_;
}
MachineInfo Emulator::info() const
{
    return machine_ ? machine_->info()
                    : MachineInfo{"el99xx",
                                  "TLCS-900/L1",
                                  el9950::kClockHz,
                                  el9950::kClockHz,
                                  el9950::kAddressMask,
                                  el9950::kRamBase,
                                  el9950::kFlashBase,
                                  132,
                                  64,
                                  false};
}
const el9950::Keyboard &Emulator::keyboard() const
{
    static const el9950::Keyboard empty;
    return machine_ ? machine_->keyboard() : empty;
}
std::span<const std::uint8_t> Emulator::framebuffer() const
{
    static constexpr std::array<std::uint8_t, 132 * 64> blank{};
    return machine_ ? machine_->framebuffer() : std::span<const std::uint8_t>(blank);
}
void Emulator::install(el9950::RomImage rom)
{
    auto replacement = rom.family() == el9950::HardwareFamily::el99xx ? make_el99_machine(std::move(rom))
                                                                      : make_el93_machine(std::move(rom));
    replacement->set_io_observer(observer_);
    machine_ = std::move(replacement);
}
void Emulator::set_operating_battery_percent(std::uint8_t percent)
{
    if (percent > 100)
        throw std::runtime_error("battery percentage exceeds 100");
    if (machine_)
        machine_->set_operating_battery_percent(percent);
}
} // namespace sharp
