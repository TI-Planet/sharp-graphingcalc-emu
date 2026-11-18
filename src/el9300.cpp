#include "sharp/el9300.hpp"
#include "el9950/state.hpp"
#include "sharp/machine.hpp"
#include "sharp/sc62015.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <type_traits>

namespace sharp
{
namespace
{
class Writer
{
  public:
    std::vector<std::uint8_t> data;
    template <class T> void integer(T value)
    {
        for (unsigned n = 0; n < sizeof(T); ++n)
            data.push_back(std::uint64_t(value) >> (n * 8));
    }
    void bytes(std::span<const std::uint8_t> bytes) { data.insert(data.end(), bytes.begin(), bytes.end()); }
};
class Reader
{
  public:
    explicit Reader(std::span<const std::uint8_t> bytes) : data(bytes) {}
    template <class T> T integer()
    {
        const auto bytes = take(sizeof(T));
        std::uint64_t value = 0;
        for (unsigned n = 0; n < sizeof(T); ++n)
            value |= std::uint64_t(bytes[n]) << (n * 8);
        return static_cast<T>(value);
    }
    std::span<const std::uint8_t> take(std::size_t size)
    {
        if (size > data.size())
            throw std::runtime_error("truncated EL-9300 state");
        const auto result = data.first(size);
        data = data.subspan(size);
        return result;
    }
    bool empty() const { return data.empty(); }

  private:
    std::span<const std::uint8_t> data;
};

class El93Machine final : public Machine, private Sc62015Bus
{
  public:
    explicit El93Machine(el9950::RomImage rom)
        : rom_(std::move(rom)), ram_(rom_.model() == el9950::Model::el9200 ? 0x2000 : 0x8000), cpu_(*this)
    {
        reset();
    }
    MachineInfo info() const override
    {
        return {"el92xx", "SC62015",
                2304000,  Sc62015::states_per_second,
                0xfffff,  std::uint32_t(0x20000 - ram_.size()),
                0xc0000,  96,
                64,       true};
    }
    const el9950::RomImage &rom() const override { return rom_; }
    void reset() override
    {
        cpu_.reset();
        cycles_ = 0;
        breakpoint_hit_ = false;
    }
    void set_reset_contact(bool open) override
    {
        const bool was_open = reset_open_;
        reset_open_ = open;
        if (was_open && !open)
            reset();
    }
    bool reset_contact_open() const override { return reset_open_; }
    std::uint64_t run_cycles(std::uint64_t budget) override
    {
        breakpoint_hit_ = false;
        if (reset_open_)
            return 0;
        const auto before = cycles_;
        while (cycles_ - before < budget)
        {
            if (!breakpoints_.empty() && breakpoints_.contains(cpu_.state().pc))
            {
                breakpoint_hit_ = true;
                break;
            }
            cycles_ += cpu_.step();
        }
        return cycles_ - before;
    }
    std::uint64_t cycles() const override { return cycles_; }
    void set_key(el9950::Key key, bool pressed) override
    {
        if (keyboard_.pressed(key) == pressed)
            return;
        if (key != el9950::Key::on && !el9300_key(key))
            throw std::runtime_error("EL-9300 logical key is not mapped yet; use raw matrix controls");
        keyboard_.set(key, pressed);
        if (pressed)
            cpu_.request_interrupt(key == el9950::Key::on ? 8 : 4);
    }
    void set_matrix_key(unsigned output, unsigned input, bool pressed) override
    {
        if (output >= 16 || input >= 8)
            throw std::runtime_error("SC62015 matrix coordinate is out of range");
        const auto bit = std::uint8_t(1U << input);
        if (pressed)
            matrix_[output] |= bit;
        else
            matrix_[output] &= ~bit;
        if (pressed)
            cpu_.request_interrupt(4);
    }
    void set_key_id(std::string_view id, bool pressed) override
    {
        if (id == "edit")
            id = "eqtn"; // Legacy headless ID for the EQTN key.
        for (const auto &key : el9300_extra_keys)
            if (key.id == id)
            {
                set_matrix_key(key.output, key.input, pressed);
                return;
            }
        Machine::set_key_id(id, pressed);
    }
    bool key_pressed(std::string_view id) const override
    {
        if (id == "edit")
            id = "eqtn";
        for (const auto &key : el9300_extra_keys)
            if (key.id == id)
            {
                const auto common = el9950::Keyboard::from_id(id);
                return (matrix_[key.output] & (1U << key.input)) || (common && keyboard_.pressed(*common));
            }
        return Machine::key_pressed(id);
    }
    std::uint8_t read_internal8(std::uint8_t address) const override { return cpu_.read_internal(address); }
    void write_internal8(std::uint8_t address, std::uint8_t value) override { cpu_.write_internal(address, value); }
    const el9950::Keyboard &keyboard() const override { return keyboard_; }
    void set_keyboard_side(el9950::KeyboardSide side) override
    {
        if (side != el9950::KeyboardSide::advanced)
            throw std::runtime_error("EL-9200/9300 has a fixed keyboard");
    }
    void set_operating_battery_percent(std::uint8_t percent) override { battery_ = percent; }
    std::uint8_t operating_battery_percent() const override { return battery_; }
    void set_backup_battery_healthy(bool healthy) override { backup_ = healthy; }
    bool backup_battery_healthy() const override { return backup_; }
    void set_irq_line(std::uint8_t line, bool asserted) override
    {
        if (line != 0)
            throw std::runtime_error("SC62015 has one external IRQ line (0)");
        if (asserted)
            cpu_.request_interrupt(0x40);
    }
    std::span<const std::uint8_t> framebuffer() const override
    {
        // Firmware F70BC's eight row origins are 61DF,615F,60DF,605F,
        // 41DF,415F,40DF,405F. Each text row writes 96 columns backwards.
        // The two drivers therefore cover upper/lower halves, with 128-byte
        // page strides. The separate annunciator wiring is still unknown.
        framebuffer_.fill(0);
        if (cpu_.read_internal(0xfd) & 1)
        {
            for (unsigned y = 0; y < 64; ++y)
                for (unsigned x = 0; x < 96; ++x)
                {
                    const auto index = (y < 32 ? 0x200 : 0) + (3 - (y % 32) / 8) * 128 + 95 - x;
                    framebuffer_[y * 96 + x] = (lcd_[index] & (1U << (y % 8))) ? 255 : 0;
                }
        }
        return framebuffer_;
    }
    CpuState cpu_state() const override
    {
        const auto s = cpu_.state();
        return {s.pc,
                s.flags,
                s.halted,
                {{"BA", s.ba, 16},
                 {"I", s.i, 16},
                 {"X", s.x, 20},
                 {"Y", s.y, 20},
                 {"U", s.u, 20},
                 {"S", s.s, 20},
                 {"BP", s.internal[0xec], 8},
                 {"PX", s.internal[0xed], 8},
                 {"PY", s.internal[0xee], 8},
                 {"IMR", s.internal[0xfb], 8},
                 {"ISR", s.internal[0xfc], 8},
                 {"SCR", s.internal[0xfd], 8},
                 {"OFF", std::uint32_t(s.off), 1}}};
    }
    std::uint8_t peek8(std::uint32_t address) const override
    {
        address &= 0xfffff;
        if (address >= 0xc0000)
            return rom_.bytes()[address - 0xc0000];
        if (address >= 0x20000 - ram_.size() && address < 0x20000)
            return ram_[address - (0x20000 - ram_.size())];
        if (const auto index = lcd_index(address); index >= 0)
            return lcd_[unsigned(index)];
        return 0xff;
    }
    void write8(std::uint32_t address, std::uint8_t value) override
    {
        address &= 0xfffff;
        if (address >= 0x20000 - ram_.size() && address < 0x20000)
        {
            ram_[address - (0x20000 - ram_.size())] = value;
            return;
        }
        if (const auto index = lcd_index(address); index >= 0)
            lcd_[unsigned(index)] = value;
        if (address < 0xc0000 && observer_)
            observer_(cycles_, true, address, value);
    }
    void set_io_observer(IoObserver observer) override { observer_ = std::move(observer); }
    el9950::DisassemblyLine disassemble(std::uint32_t address) const override { return cpu_.disassemble(address); }
    std::vector<std::uint8_t> save_state() const override
    {
        Writer w;
        constexpr std::array<std::uint8_t, 8> magic{'E', 'L', '9', '3', 'S', 'T', 'A', 'T'};
        w.bytes(magic);
        w.integer<std::uint32_t>(1);
        w.bytes(rom_.digest());
        w.integer(cycles_);
        const auto s = cpu_.state();
        for (const auto v : {s.pc, s.x, s.y, s.u, s.s})
            w.integer(v);
        w.integer(s.ba);
        w.integer(s.i);
        w.integer(s.flags);
        w.bytes(s.internal);
        w.integer(s.main_timer);
        w.integer(s.sub_timer);
        w.integer<std::uint8_t>(s.halted);
        w.integer<std::uint8_t>(s.off);
        w.integer<std::uint32_t>(std::uint32_t(ram_.size()));
        w.bytes(ram_);
        w.bytes(lcd_);
        w.bytes(matrix_);
        w.integer(keyboard_.pressed_mask());
        w.integer(battery_);
        w.integer<std::uint8_t>(backup_);
        w.integer<std::uint8_t>(reset_open_);
        w.integer(el9950::crc32(w.data));
        return w.data;
    }
    void load_state(std::span<const std::uint8_t> bytes) override
    {
        if (bytes.size() < 4)
            throw std::runtime_error("truncated EL-9300 state");
        Reader checksum(bytes.last(4));
        if (checksum.integer<std::uint32_t>() != el9950::crc32(bytes.first(bytes.size() - 4)))
            throw std::runtime_error("EL-9300 state CRC mismatch");
        Reader r(bytes.first(bytes.size() - 4));
        const auto magic = r.take(8);
        constexpr std::array<std::uint8_t, 8> expected{'E', 'L', '9', '3', 'S', 'T', 'A', 'T'};
        if (!std::equal(magic.begin(), magic.end(), expected.begin()) || r.integer<std::uint32_t>() != 1)
            throw std::runtime_error("unsupported EL-9300 state format");
        const auto digest = r.take(32);
        if (!std::equal(digest.begin(), digest.end(), rom_.digest().begin()))
            throw std::runtime_error("EL-9300 state ROM mismatch");
        const auto cycles = r.integer<std::uint64_t>();
        Sc62015State s;
        s.pc = r.integer<std::uint32_t>();
        s.x = r.integer<std::uint32_t>();
        s.y = r.integer<std::uint32_t>();
        s.u = r.integer<std::uint32_t>();
        s.s = r.integer<std::uint32_t>();
        s.ba = r.integer<std::uint16_t>();
        s.i = r.integer<std::uint16_t>();
        s.flags = r.integer<std::uint8_t>();
        const auto internal = r.take(256);
        std::copy(internal.begin(), internal.end(), s.internal.begin());
        s.main_timer = r.integer<std::uint64_t>();
        s.sub_timer = r.integer<std::uint64_t>();
        const auto halted = r.integer<std::uint8_t>(), off = r.integer<std::uint8_t>();
        s.halted = halted;
        s.off = off;
        if (r.integer<std::uint32_t>() != ram_.size())
            throw std::runtime_error("EL-9300 state RAM size mismatch");
        const auto ram = r.take(ram_.size()), lcd = r.take(lcd_.size());
        const auto matrix = r.take(16);
        const auto keys = r.integer<std::uint64_t>();
        const auto battery = r.integer<std::uint8_t>(), backup = r.integer<std::uint8_t>(),
                   reset = r.integer<std::uint8_t>();
        if (!r.empty() || battery > 100 || backup > 1 || reset > 1 || halted > 1 || off > 1 ||
            (keys >> unsigned(el9950::Key::count)))
            throw std::runtime_error("invalid EL-9300 state data");
        // Validate everything before changing a running machine.
        cpu_.set_state(s);
        std::copy(ram.begin(), ram.end(), ram_.begin());
        std::copy(lcd.begin(), lcd.end(), lcd_.begin());
        std::copy(matrix.begin(), matrix.end(), matrix_.begin());
        cycles_ = cycles;
        keyboard_.restore_mask(keys);
        battery_ = battery;
        backup_ = backup;
        reset_open_ = reset;
        breakpoint_hit_ = false;
    }
    void add_breakpoint(std::uint32_t address) override { breakpoints_.insert(address & 0xfffff); }
    void remove_breakpoint(std::uint32_t address) override { breakpoints_.erase(address & 0xfffff); }
    const std::unordered_set<std::uint32_t> &breakpoints() const override { return breakpoints_; }
    bool breakpoint_hit() const override { return breakpoint_hit_; }
    el9950::LinkPort &link() override { return link_; }

  private:
    static int lcd_index(std::uint32_t address)
    {
        if (address >= 0x4000 && address < 0x4200)
            return int(address - 0x4000);
        if (address >= 0x6000 && address < 0x6200)
            return int(address - 0x6000 + 0x200);
        return -1;
    }
    std::uint8_t read8(std::uint32_t address) override
    {
        const auto value = peek8(address);
        if (observer_ && address < 0xc0000 && !(address >= 0x20000 - ram_.size() && address < 0x20000))
            observer_(cycles_, false, address, value);
        return value;
    }
    std::uint8_t input(std::uint8_t port, std::uint8_t latch) const override
    {
        std::uint8_t value = latch;
        if (port == 0xf2)
        {
            value = 0;
            const auto outputs = cpu_.read_internal(0xf0) | (unsigned(cpu_.read_internal(0xf1)) << 8);
            for (unsigned row = 0; row < 16; ++row)
                if (outputs & (1U << row))
                    value |= matrix_[row];
            // F7324-F733E independently isolates CL to KO6/KI2.
            for (unsigned index = 0; index < unsigned(el9950::Key::count); ++index)
            {
                const auto key = static_cast<el9950::Key>(index);
                if (!keyboard_.pressed(key))
                    continue;
                if (const auto coordinate = el9300_key(key); coordinate && (outputs & (1U << coordinate->first)))
                    value |= 1U << coordinate->second;
            }
        }
        if (port == 0xf5)
            value = backup_ ? 0x10 : 0; // F6F28/F6F31: backup-battery check
        if (port == 0xf6)
            value = 0;
        if (port == 0xff)
            value = (latch & ~8U) | (keyboard_.on_pressed() ? 8 : 0);
        if (observer_)
            observer_(cycles_, false, 0x100000U + port, value);
        return value;
    }
    void output(std::uint8_t port, std::uint8_t value) override
    {
        if (observer_)
            observer_(cycles_, true, 0x100000U + port, value);
    }
    el9950::RomImage rom_;
    std::vector<std::uint8_t> ram_;
    std::array<std::uint8_t, 1024> lcd_{};
    std::array<std::uint8_t, 16> matrix_{};
    mutable std::array<std::uint8_t, 96 * 64> framebuffer_{};
    Sc62015 cpu_;
    el9950::Keyboard keyboard_;
    el9950::LinkPort link_;
    IoObserver observer_;
    std::unordered_set<std::uint32_t> breakpoints_;
    std::uint64_t cycles_{};
    std::uint8_t battery_{100};
    bool backup_{true}, reset_open_{}, breakpoint_hit_{};
};
} // namespace
std::unique_ptr<Machine> make_el93_machine(el9950::RomImage rom)
{
    return std::make_unique<El93Machine>(std::move(rom));
}
} // namespace sharp
