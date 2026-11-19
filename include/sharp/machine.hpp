#pragma once

#include "el9950/bus.hpp"
#include "el9950/debugger.hpp"
#include "el9950/keyboard.hpp"
#include "el9950/link.hpp"
#include "el9950/rom.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace sharp
{
using IoObserver = el9950::Bus::IoObserver;
struct MachineInfo
{
    std::string_view id, cpu;
    std::uint32_t clock_hz, cycles_per_second, address_mask, ram_base, rom_base;
    unsigned lcd_width, lcd_height;
    bool experimental;
};
struct DebugRegister
{
    std::string name;
    std::uint32_t value;
    unsigned bits;
};
struct CpuState
{
    std::uint32_t pc{}, sr{};
    bool halted{};
    std::vector<DebugRegister> registers;
};

// Bits in the physical left-to-right order of the EL-9200/9300 LCD.
enum class LcdAnnunciator : std::uint16_t
{
    left = 1U << 0,
    busy = 1U << 1,
    second = 1U << 2,
    alpha = 1U << 3,
    insert = 1U << 4,
    battery = 1U << 5,
    up = 1U << 6,
    down = 1U << 7,
    right = 1U << 8,
};

// The frontend sees a complete machine, not an interchangeable CPU attached
// to another family's bus. Registers, disassembly and serialization belong
// to the backend. EL-99xx keeps its existing state format and implementation.
class Machine
{
  public:
    virtual ~Machine() = default;
    virtual MachineInfo info() const = 0;
    virtual const el9950::RomImage &rom() const = 0;
    virtual void reset() = 0;
    virtual void set_reset_contact(bool open) = 0;
    virtual bool reset_contact_open() const = 0;
    virtual std::uint64_t run_cycles(std::uint64_t budget) = 0;
    virtual std::uint64_t cycles() const = 0;
    virtual void set_key(el9950::Key key, bool pressed) = 0;
    virtual void set_key_id(std::string_view id, bool pressed)
    {
        const auto key = el9950::Keyboard::from_id(id);
        if (!key)
            throw std::runtime_error("unknown key id: " + std::string(id));
        set_key(*key, pressed);
    }
    virtual bool key_pressed(std::string_view id) const
    {
        const auto key = el9950::Keyboard::from_id(id);
        if (!key)
            throw std::runtime_error("unknown key id: " + std::string(id));
        return keyboard().pressed(*key);
    }
    virtual void set_matrix_key(unsigned output, unsigned input, bool pressed)
    {
        (void)output;
        (void)input;
        (void)pressed;
        throw std::runtime_error("raw matrix access is unavailable for this machine");
    }
    virtual std::uint8_t read_internal8(std::uint8_t address) const
    {
        (void)address;
        throw std::runtime_error("this CPU has no separate internal address space");
    }
    virtual void write_internal8(std::uint8_t address, std::uint8_t value)
    {
        (void)address;
        (void)value;
        throw std::runtime_error("this CPU has no separate internal address space");
    }
    virtual const el9950::Keyboard &keyboard() const = 0;
    virtual void set_keyboard_side(el9950::KeyboardSide side) = 0;
    virtual void set_operating_battery_percent(std::uint8_t percent) = 0;
    virtual std::uint8_t operating_battery_percent() const = 0;
    virtual void set_backup_battery_healthy(bool healthy) = 0;
    virtual bool backup_battery_healthy() const = 0;
    virtual void set_irq_line(std::uint8_t line, bool asserted) = 0;
    virtual std::span<const std::uint8_t> framebuffer() const = 0;
    virtual std::uint16_t lcd_annunciators() const { return 0; }
    virtual CpuState cpu_state() const = 0;
    virtual std::vector<DebugRegister> device_registers() const { return {}; }
    virtual std::uint8_t peek8(std::uint32_t address) const = 0;
    virtual void write8(std::uint32_t address, std::uint8_t value) = 0;
    virtual void set_io_observer(IoObserver observer) = 0;
    virtual el9950::DisassemblyLine disassemble(std::uint32_t address) const = 0;
    virtual std::vector<std::uint8_t> save_state() const = 0;
    virtual void load_state(std::span<const std::uint8_t> bytes) = 0;
    virtual void add_breakpoint(std::uint32_t address) = 0;
    virtual void remove_breakpoint(std::uint32_t address) = 0;
    virtual const std::unordered_set<std::uint32_t> &breakpoints() const = 0;
    virtual bool breakpoint_hit() const = 0;
    virtual el9950::LinkPort &link() = 0;
};

std::unique_ptr<Machine> make_el99_machine(el9950::RomImage rom);
std::unique_ptr<Machine> make_el93_machine(el9950::RomImage rom);

// ROM loading constructs and validates the replacement before switching.
// A failed load leaves the running calculator and its state untouched.
class Emulator
{
  public:
    void load_rom(const std::filesystem::path &path) { install(el9950::RomImage::load(path)); }
    void load_rom(std::vector<std::uint8_t> bytes) { install(el9950::RomImage::from_bytes(std::move(bytes))); }
    bool has_rom() const { return bool(machine_); }
    MachineInfo info() const;
    const el9950::RomImage &rom() const { return machine().rom(); }
    bool known_rom() const { return has_rom() && rom().is_known_120(); }
    void reset()
    {
        if (machine_)
            machine_->reset();
    }
    void set_reset_contact(bool open)
    {
        if (machine_)
            machine_->set_reset_contact(open);
    }
    bool reset_contact_open() const { return machine_ && machine_->reset_contact_open(); }
    std::uint64_t run_cycles(std::uint64_t budget) { return machine_ ? machine_->run_cycles(budget) : 0; }
    std::uint64_t cycles() const { return machine_ ? machine_->cycles() : 0; }
    void set_key(el9950::Key key, bool pressed)
    {
        if (machine_)
            machine_->set_key(key, pressed);
    }
    void set_key(std::string_view id, bool pressed) { machine().set_key_id(id, pressed); }
    bool key_pressed(std::string_view id) const { return machine_ && machine_->key_pressed(id); }
    void set_matrix_key(unsigned output, unsigned input, bool pressed)
    {
        machine().set_matrix_key(output, input, pressed);
    }
    std::uint8_t read_internal8(std::uint8_t address) const { return machine().read_internal8(address); }
    void write_internal8(std::uint8_t address, std::uint8_t value) { machine().write_internal8(address, value); }
    const el9950::Keyboard &keyboard() const;
    el9950::KeyboardSide keyboard_side() const { return keyboard().side(); }
    void set_keyboard_side(el9950::KeyboardSide side) { machine().set_keyboard_side(side); }
    void set_operating_battery_percent(std::uint8_t percent);
    std::uint8_t operating_battery_percent() const { return machine_ ? machine_->operating_battery_percent() : 100; }
    void set_backup_battery_healthy(bool healthy)
    {
        if (machine_)
            machine_->set_backup_battery_healthy(healthy);
    }
    bool backup_battery_healthy() const { return !machine_ || machine_->backup_battery_healthy(); }
    void set_irq_line(std::uint8_t line, bool asserted) { machine().set_irq_line(line, asserted); }
    void pulse_irq(std::uint8_t line)
    {
        set_irq_line(line, true);
        set_irq_line(line, false);
    }
    std::span<const std::uint8_t> framebuffer() const;
    std::uint16_t lcd_annunciators() const { return machine_ ? machine_->lcd_annunciators() : 0; }
    CpuState cpu_state() const { return machine().cpu_state(); }
    std::vector<DebugRegister> device_registers() const { return machine().device_registers(); }
    std::uint8_t peek8(std::uint32_t address) const { return machine().peek8(address); }
    void write8(std::uint32_t address, std::uint8_t value) { machine().write8(address, value); }
    void set_io_observer(IoObserver observer)
    {
        observer_ = std::move(observer);
        if (machine_)
            machine_->set_io_observer(observer_);
    }
    el9950::DisassemblyLine disassemble(std::uint32_t address) const { return machine().disassemble(address); }
    std::vector<std::uint8_t> save_state() const { return machine().save_state(); }
    void load_state(std::span<const std::uint8_t> bytes) { machine().load_state(bytes); }
    std::string_view state_format() const { return info().id == "el99xx" ? "el99state" : "el93state"; }
    void add_breakpoint(std::uint32_t address) { machine().add_breakpoint(address); }
    void remove_breakpoint(std::uint32_t address) { machine().remove_breakpoint(address); }
    const std::unordered_set<std::uint32_t> &breakpoints() const { return machine().breakpoints(); }
    bool breakpoint_hit() const { return machine_ && machine_->breakpoint_hit(); }
    el9950::LinkPort &link() { return machine().link(); }

  private:
    Machine &machine();
    const Machine &machine() const;
    void install(el9950::RomImage rom);
    std::unique_ptr<Machine> machine_;
    IoObserver observer_;
};
} // namespace sharp
