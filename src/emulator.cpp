#include "el9950/emulator.hpp"
#include "el9950/constants.hpp"
#include "el9950/state.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace el9950
{
    namespace
    {
        class Writer
        {
        public:
            template <typename T>
            void
            integer(T value) {
                static_assert(std::is_integral_v<T>);
                using U = std::make_unsigned_t<T>;
                for (std::size_t i = 0; i < sizeof(T); ++i)
                    data.push_back(static_cast<std::uint8_t>(U(value) >> (i * 8)));
            }

            void
            bytes(std::span<const std::uint8_t> value) { data.insert(data.end(), value.begin(), value.end()); }

            std::vector<std::uint8_t> data;
        };

        class Reader
        {
        public:
            explicit
            Reader(std::span<const std::uint8_t> input)
            : input_(input) {}

            template <typename T>
            T
            integer() {
                static_assert(std::is_integral_v<T>);
                if (position_ + sizeof(T) > input_.size())
                    throw std::runtime_error("truncated save state");
                using U = std::make_unsigned_t<T>;
                U value{};
                for (std::size_t i = 0; i < sizeof(T); ++i)
                    value |= U(input_[position_++]) << (i * 8);
                return static_cast<T>(value);
            }

            std::span<const std::uint8_t>
            bytes(std::size_t count) {
                if (position_ + count > input_.size())
                    throw std::runtime_error("truncated save state");
                const auto result = input_.subspan(position_, count);
                position_ += count;
                return result;
            }

            bool
            done() const { return position_ == input_.size(); }

        private:
            std::span<const std::uint8_t> input_;
            std::size_t position_{};
        };

        void write_cpu(Writer& w, const CpuState& s) {
            w.integer(s.pc);
            w.integer(s.sr);
            for (const auto v : s.xwa_xbc_xde_xhl)
                w.integer(v);
            w.integer(s.xix);
            w.integer(s.xiy);
            w.integer(s.xiz);
            w.integer(s.xnsp);
            w.integer(s.xssp);
            for (const auto v : s.dma)
                w.integer(v);
            w.integer(s.register_bank);
            w.integer<std::uint8_t>(s.halted);
        }

        CpuState read_cpu(Reader& r) {
            CpuState s;
            s.pc = r.integer<std::uint32_t>();
            s.sr = r.integer<std::uint32_t>();
            for (auto& v : s.xwa_xbc_xde_xhl)
                v = r.integer<std::uint32_t>();
            s.xix = r.integer<std::uint32_t>();
            s.xiy = r.integer<std::uint32_t>();
            s.xiz = r.integer<std::uint32_t>();
            s.xnsp = r.integer<std::uint32_t>();
            s.xssp = r.integer<std::uint32_t>();
            for (auto& v : s.dma)
                v = r.integer<std::uint32_t>();
            s.register_bank = r.integer<std::uint8_t>();
            s.halted = r.integer<std::uint8_t>() != 0;
            return s;
        }
    }

    Emulator::Emulator()
    : mcu_(keyboard_, link_), bus_(flash_, lcd_, mcu_), cpu_(make_tlcs900_l1_cpu(bus_)) {
        flash_.set_boot_write_callback([this] {
            if (rom_ && !creating_checkpoint_) {
                creating_checkpoint_ = true;
                last_boot_checkpoint_ = save_state();
                creating_checkpoint_ = false;
            }
        });
    }

    void Emulator::load_rom(const std::filesystem::path& path) { install_rom(RomImage::load(path)); }
    void Emulator::load_rom(std::vector<std::uint8_t> bytes) { install_rom(RomImage::from_bytes(std::move(bytes))); }

    const RomImage& Emulator::rom() const {
        if (!rom_)
            throw std::runtime_error("no ROM loaded");
        return *rom_;
    }

    void Emulator::install_rom(RomImage image) {
        rom_ = std::move(image);
        keyboard_.reset();
        keyboard_.set_side(KeyboardSide::advanced);
        flash_.load(rom_->bytes());
        cycles_ = 0;
        reset_contact_open_ = false;
        last_boot_checkpoint_.clear();
        reset();
    }

    void Emulator::set_keyboard_side(KeyboardSide side) {
        if (side != KeyboardSide::advanced && side != KeyboardSide::basic)
            throw std::runtime_error("invalid keyboard side");
        if (side == KeyboardSide::basic && (!rom_ || !has_reversible_keyboard(rom_->model())))
            throw std::runtime_error("this ROM has no reversible keyboard");
        keyboard_.set_side(side);
    }

    void Emulator::reset() {
        if (!rom_)
            return;
        bus_.reset();
        cpu_->reset();
        cycles_ = 0;
    }

    void Emulator::set_reset_contact(bool open) {
        if (open == reset_contact_open_)
            return;
        const bool was_open = reset_contact_open_;
        reset_contact_open_ = open;
        if (was_open && !open)
            reset();
    }

    void Emulator::set_key(Key key, bool pressed) {
        if (keyboard_.pressed(key) == pressed)
            return;
        keyboard_.set(key, pressed);

        // ON uses the external INT1 source. All scanned keys use the TMP91C025's
        // dedicated, edge-triggered INTKEY source. The electrical key level remains
        // asserted separately so held chords can be identified after wake-up.
        if (pressed)
            mcu_.request_interrupt(key == Key::on ? 11 : 28);
    }

    std::uint64_t Emulator::run_cycles(std::uint64_t requested) {
        if (!rom_ || reset_contact_open_)
            return 0;
        breakpoint_hit_ = false;
        std::uint64_t total = 0;
        while (total < requested) {
            if (breakpoints_.contains(cpu_->state().pc & kAddressMask)) {
                breakpoint_hit_ = true;
                break;
            }
            const auto maximum = breakpoints_.empty() ? 4096U : 1U;
            const auto chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(requested - total, maximum));
            const auto executed = cpu_->execute(chunk);
            if (!executed)
                break;
            total += executed;
        }
        cycles_ += total;
        return total;
    }

    std::vector<std::uint8_t> Emulator::save_state() const {
        if (!rom_)
            throw std::runtime_error("cannot save state without ROM");
        Writer w;
        constexpr std::array<std::uint8_t, 8> magic{'E', 'L', '9', '9', 'S', 'T', 'A', 'T'};
        w.bytes(magic);
        w.integer(kStateVersion);
        w.bytes(rom_->digest());
        w.integer(cycles_);
        write_cpu(w, cpu_->state());
        w.integer<std::uint32_t>(static_cast<std::uint32_t>(bus_.ram().size()));
        w.bytes(bus_.ram());
        std::vector<std::pair<std::uint32_t, std::uint8_t>> overlay(flash_.overlay().begin(), flash_.overlay().end());
        std::sort(overlay.begin(), overlay.end());
        w.integer<std::uint32_t>(static_cast<std::uint32_t>(overlay.size()));
        for (const auto& [offset,value] : overlay) {
            w.integer(offset);
            w.integer(value);
        }
        w.bytes(mcu_.registers());
        w.integer(mcu_.timer_ticks());
        w.integer(mcu_.pending_interrupts());
        for (const auto accumulator : mcu_.timer_accumulators())
            w.integer(accumulator);
        w.integer(keyboard_.pressed_mask());
        w.integer(mcu_.operating_battery_percent());
        w.integer<std::uint8_t>(mcu_.backup_battery_healthy());
        const auto ls = lcd_.state();
        w.bytes(ls.vram);
        w.integer(ls.page);
        w.integer(ls.column);
        w.integer(ls.start_line);
        w.integer(ls.contrast);
        w.integer<std::uint8_t>(ls.display_on);
        w.integer<std::uint8_t>(ls.inverted);
        w.integer<std::uint8_t>(ls.adc_reverse);
        w.integer<std::uint8_t>(ls.com_reverse);
        w.integer<std::uint8_t>(ls.all_pixels);
        w.integer<std::uint8_t>(ls.expect_contrast);
        for (const bool released : link_.drive_state())
            w.integer<std::uint8_t>(released);
        w.integer<std::uint8_t>(reset_contact_open_);
        w.integer(static_cast<std::uint8_t>(keyboard_.side()));
        w.integer(crc32(w.data));
        return std::move(w.data);
    }

    void Emulator::load_state(std::span<const std::uint8_t> bytes) {
        if (!rom_)
            throw std::runtime_error("cannot load state without ROM");
        if (bytes.size() < 4)
            throw std::runtime_error("truncated save state");
        const auto expected = std::uint32_t(bytes[bytes.size() - 4]) | std::uint32_t(bytes[bytes.size() - 3]) << 8 |
            std::uint32_t(bytes[bytes.size() - 2]) << 16 | std::uint32_t(bytes[bytes.size() - 1]) << 24;
        if (crc32(bytes.first(bytes.size() - 4)) != expected)
            throw std::runtime_error("save state CRC mismatch");
        Reader r(bytes.first(bytes.size() - 4));
        constexpr std::array<std::uint8_t, 8> magic{'E', 'L', '9', '9', 'S', 'T', 'A', 'T'};
        const auto saved_magic = r.bytes(magic.size());
        if (!std::equal(magic.begin(), magic.end(), saved_magic.begin()))
            throw std::runtime_error("invalid save state magic");
        const auto state_version = r.integer<std::uint32_t>();
        if (state_version < 4 || state_version > kStateVersion)
            throw std::runtime_error("unsupported save state version");
        const auto digest = r.bytes(32);
        if (!std::equal(digest.begin(), digest.end(), rom_->digest().begin()))
            throw std::runtime_error("save state ROM SHA mismatch");
        const auto new_cycles = r.integer<std::uint64_t>();
        const auto new_cpu = read_cpu(r);
        const auto ram_size = r.integer<std::uint32_t>();
        if (ram_size != bus_.ram().size())
            throw std::runtime_error("save state RAM size mismatch");
        const auto ram = r.bytes(ram_size);
        std::unordered_map<std::uint32_t, std::uint8_t> overlay;
        const auto overlay_count = r.integer<std::uint32_t>();
        if (overlay_count > kFlashSize)
            throw std::runtime_error("invalid flash overlay size");
        for (std::uint32_t i = 0; i < overlay_count; ++i) {
            const auto offset = r.integer<std::uint32_t>();
            const auto value = r.integer<std::uint8_t>();
            if (offset >= kFlashSize)
                throw std::runtime_error("invalid flash overlay offset");
            overlay[offset] = value;
        }
        std::array<std::uint8_t, kInternalIoSize> regs{};
        const auto regs_bytes = r.bytes(regs.size());
        std::copy(regs_bytes.begin(), regs_bytes.end(), regs.begin());
        const auto ticks = r.integer<std::uint64_t>();
        const auto pending_interrupts = state_version >= 5 ? r.integer<std::uint64_t>() : 0;
        std::array<std::uint32_t, 6> timer_accumulators{};
        if (state_version >= 6)
            for (auto& accumulator : timer_accumulators)
                accumulator = r.integer<std::uint32_t>();
        const auto keymask = r.integer<std::uint64_t>();
        const auto operating_battery_percent = r.integer<std::uint8_t>();
        if (operating_battery_percent > 100)
            throw std::runtime_error("invalid operating battery percentage");
        const auto backup_battery_healthy = r.integer<std::uint8_t>() != 0;
        LcdState ls;
        const auto vram = r.bytes(ls.vram.size());
        std::copy(vram.begin(), vram.end(), ls.vram.begin());
        ls.page = r.integer<std::uint8_t>();
        ls.column = r.integer<std::uint8_t>();
        ls.start_line = r.integer<std::uint8_t>();
        ls.contrast = r.integer<std::uint8_t>();
        ls.display_on = r.integer<std::uint8_t>();
        ls.inverted = r.integer<std::uint8_t>();
        ls.adc_reverse = r.integer<std::uint8_t>();
        ls.com_reverse = r.integer<std::uint8_t>();
        ls.all_pixels = r.integer<std::uint8_t>();
        ls.expect_contrast = r.integer<std::uint8_t>();
        std::array<bool, 3> link{};
        for (auto& line : link)
            line = r.integer<std::uint8_t>() != 0;
        const auto reset_open = r.integer<std::uint8_t>() != 0;
        const auto side = state_version >= 7 ? r.integer<std::uint8_t>() : 0;
        if (side > 1 || (side == 1 && !has_reversible_keyboard(rom_->model())))
            throw std::runtime_error("invalid keyboard side for this ROM");
        if (!r.done())
            throw std::runtime_error("unexpected trailing save state data");
        std::copy(ram.begin(), ram.end(), bus_.ram().begin());
        flash_.restore_overlay(std::move(overlay));
        mcu_.restore_registers(regs);
        mcu_.restore_timer_ticks(ticks);
        mcu_.restore_pending_interrupts(pending_interrupts);
        mcu_.restore_timer_accumulators(timer_accumulators);
        keyboard_.restore_mask(keymask);
        keyboard_.set_side(static_cast<KeyboardSide>(side));
        mcu_.set_operating_battery_percent(operating_battery_percent);
        mcu_.set_backup_battery_healthy(backup_battery_healthy);
        lcd_.set_state(ls);
        link_.restore(link);
        cpu_->set_state(new_cpu);
        cycles_ = new_cycles;
        reset_contact_open_ = reset_open;
    }
} // namespace el9950
