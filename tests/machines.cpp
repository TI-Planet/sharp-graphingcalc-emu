#include "el9950/constants.hpp"
#include "el9950/emulator.hpp"
#include "el9950/skin.hpp"
#include "el9950/state.hpp"
#include "sharp/machine.hpp"
#include "sharp/sc62015.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f)
{
    bool rejected = false;
    try
    {
        f();
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    check(rejected, "invalid operation was accepted");
}
class TestBus final : public sharp::Sc62015Bus
{
  public:
    std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(0x100000);
    std::uint8_t read8(std::uint32_t a) override { return memory.at(a); }
    std::uint8_t peek8(std::uint32_t a) const override { return memory.at(a); }
    void write8(std::uint32_t a, std::uint8_t v) override { memory.at(a) = v; }
    void program(std::uint32_t address, std::initializer_list<std::uint8_t> bytes)
    {
        std::copy(bytes.begin(), bytes.end(), memory.begin() + address);
    }
};

void cpu_addressing_and_flags()
{
    TestBus bus;
    bus.program(0xffffd, {0x00, 0x01, 0x0c});
    bus.program(0xc0100, {
                             0x08, 0xff,             // MV A,FF
                             0x40, 0x01,             // ADD A,1 -> C=Z=1
                             0x09, 0x88,             // MV IL,88 (must preserve flags and clear IH)
                             0x30, 0xcc, 0xec, 0xf0, // direct BP=F0
                             0xcc, 0x20, 0xa5,       // default BP-relative (wrap F0+20 to 10)
                             0x32, 0xc8, 0x11, 0x10, // direct/direct copy
                             0x0c, 0xfe, 0xff, 0x0f, // X=FFFFE
                             0xb0, 0x24,             // MV [X++],A (X=FFFFF)
                             0xb0, 0x24              // MV [X++],A (X wraps to 0)
                         });
    sharp::Sc62015 cpu(bus);
    cpu.reset();
    check(cpu.state().pc == 0xc0100, "reset vector");
    cpu.step();
    cpu.step();
    check(cpu.state().ba == 0 && cpu.state().flags == 3, "ADD carry/zero");
    cpu.step();
    check(cpu.state().i == 0x88 && cpu.state().flags == 3, "IL and flag preservation");
    cpu.step();
    cpu.step();
    cpu.step();
    check(cpu.read_internal(0x10) == 0xa5 && cpu.read_internal(0x11) == 0xa5, "internal prebytes and wrapping");
    cpu.step();
    cpu.step();
    cpu.step();
    check(cpu.state().x == 0, "20-bit indexed pointer wrap");
    // Multi-byte internal loads wrap at FF without reading past the array.
    auto state = cpu.state();
    state.pc = 0xc0200;
    state.internal[0xff] = 0x12;
    state.internal[0] = 0x34;
    cpu.set_state(state);
    bus.program(0xc0200, {0x30, 0x82, 0xff});
    cpu.step();
    check(cpu.state().ba == 0x3412, "internal word wraps at FF");
    // Instruction operands wrap inside a segment; data pointers do not.
    state = cpu.state();
    state.pc = 0xcffff;
    cpu.set_state(state);
    bus.program(0xcffff, {0x0a});
    bus.program(0xc0000, {0x78, 0x56});
    check(cpu.disassemble(0xcffff).length == 3, "segmented disassembly length");
    check(cpu.disassemble(0xcffff).next_address == 0xc0002, "segmented disassembly continuation");
    cpu.step();
    check(cpu.state().pc == 0xc0002 && cpu.state().ba == 0x5678, "segmented immediate fetch");
    state = cpu.state();
    state.pc = 0xc0300;
    cpu.set_state(state);
    bus.program(0xc0300, {0x12, 0xfe});
    cpu.step();
    check(cpu.state().pc == 0xc0400, "unsigned forward JR");
    bus.program(0xc0400, {0x13, 0x02});
    cpu.step();
    check(cpu.state().pc == 0xc0400, "backward JR");
    check(cpu.disassemble(0xc0400).text == "JR $C0400", "JR disassembly target");
    state = cpu.state();
    state.ba = 0x1234;
    state.flags = 3;
    state.internal[0x20] = 0x5a;
    state.internal[0xfb] = 0xff;
    state.internal[0xfd] = 0xff;
    cpu.set_state(state);
    bus.program(0xffffd, {0x00, 0x01, 0x0c}); // Restore vector overwritten by the pointer-wrap test.
    cpu.reset();
    check(cpu.state().pc == 0xc0100 && cpu.state().ba == 0x1234 && cpu.state().flags == 3 &&
              cpu.read_internal(0x20) == 0x5a && cpu.read_internal(0xfb) == 0 && cpu.read_internal(0xfd) == 0,
          "hardware reset retains registers and internal RAM while resetting control registers");
}

void cpu_calls_interrupts_and_power()
{
    TestBus bus;
    sharp::Sc62015 cpu(bus);
    bus.program(0xffffd, {0, 1, 0x0c});
    bus.program(0xffffa, {0, 2, 0x0e});
    bus.program(0xc0100, {0x05, 0, 3, 0x0d, 0x00});
    bus.program(0xd0300, {0x07});
    bus.program(0xe0200, {0x01});
    cpu.reset();
    auto state = cpu.state();
    state.s = 0x1ffff;
    cpu.set_state(state);
    cpu.step();
    check(cpu.state().pc == 0xd0300 && cpu.state().s == 0x1fffc, "CALLF frame");
    cpu.step();
    check(cpu.state().pc == 0xc0104 && cpu.state().s == 0x1ffff, "RETF frame");
    cpu.write_internal(0xfb, 0x88);
    cpu.request_interrupt(8);
    cpu.step();
    check(cpu.state().pc == 0xe0200 && cpu.state().s == 0x1fffa && cpu.read_internal(0xfb) == 8,
          "hardware interrupt frame and masking");
    cpu.write_internal(0xfc, 0);
    cpu.step();
    check(cpu.state().pc == 0xc0104 && cpu.read_internal(0xfb) == 0x88, "RETI restores IMR and PC");
    state = cpu.state();
    state.pc = 0xc0400;
    state.internal[0xfb] = 0;
    state.internal[0xfc] = 0;
    cpu.set_state(state);
    bus.program(0xc0400, {0xdf, 0x08, 0x42});
    cpu.step();
    check(cpu.state().halted && cpu.state().off, "OFF stops the CPU");
    const auto before = cpu.state();
    for (unsigned n = 0; n < 100; ++n)
        cpu.step();
    check(cpu.state().pc == before.pc && cpu.state().sub_timer == before.sub_timer, "OFF stops both clocks");
    cpu.request_interrupt(8);
    cpu.step();
    check(!cpu.state().halted && (cpu.state().ba & 255) == 0x42, "ON wakes OFF with IMR disabled");
    state = cpu.state();
    state.pc = 0xc0500;
    state.internal[0xfc] = 0;
    state.sub_timer = sharp::Sc62015::states_per_second / 2 - 1;
    cpu.set_state(state);
    bus.program(0xc0500, {0xde});
    cpu.step();
    check(cpu.read_internal(0xfc) & 2, "HALT keeps the subclock alive");
    cpu.step();
    check(!cpu.state().halted, "subclock wakes HALT");
    state = cpu.state();
    state.pc = 0xc0600;
    cpu.set_state(state);
    bus.program(0xc0600, {0x20});
    rejects([&] { cpu.step(); });
    state.pc = 0xc0700;
    cpu.set_state(state);
    std::fill_n(bus.memory.begin() + 0xc0700, 32, 0x30);
    rejects([&] { cpu.step(); });
}

void opcode_decode_consistency()
{
    // Independent decoder/engine agreement for every sequential opcode, plus
    // exercising all retained handlers under ASan/UBSan. Small I bounds block
    // instructions; external targets and both stacks point into the test bus.
    for (unsigned opcode = 0; opcode < 256; ++opcode)
    {
        if (opcode == 0x20 || opcode == 0xbf)
            continue;
        TestBus bus;
        sharp::Sc62015 cpu(bus);
        bus.program(0xc1000, {std::uint8_t(opcode), 0x04, 0x20, 0, 0, 0, 0, 0});
        sharp::Sc62015State s;
        s.pc = 0xc1000;
        s.x = 0x2000;
        s.y = 0x3000;
        s.u = 0x5000;
        s.s = 0x6000;
        s.i = 3;
        s.internal[0xec] = 0x20;
        cpu.set_state(s);
        const auto line = cpu.disassemble(s.pc);
        cpu.step();
        const bool control = opcode <= 7 || (opcode >= 0x10 && opcode <= 0x1f) || (opcode >= 0x21 && opcode <= 0x27) ||
                             (opcode >= 0x30 && opcode <= 0x37) || opcode == 0xfe || opcode == 0xff;
        if (!control)
            check(cpu.state().pc == s.pc + line.length,
                  ("opcode/disassembly length mismatch: " + std::to_string(opcode)).c_str());
    }
}

std::vector<std::uint8_t> sc_rom(bool low_ram = false)
{
    std::vector<std::uint8_t> bytes(0x40000, 0);
    const std::string banner = low_ram ? "EL 9200   Ver. 1.11" : "EL 9300   Ver. 1.11";
    std::copy(banner.begin(), banner.end(), bytes.begin() + 0x1422);
    bytes[0x3fffd] = 0;
    bytes[0x3fffe] = 1;
    bytes[0x3ffff] = 0x0c;
    bytes[0x100] = 0xde;
    return bytes;
}
std::vector<std::uint8_t> tlcs_rom()
{
    std::vector<std::uint8_t> bytes(0x100000, 0xff);
    bytes[0] = 0;
    bytes[1] = 5; // NOP; HALT
    bytes[0xffff00 - 0xf00000] = 0;
    bytes[0xffff01 - 0xf00000] = 0;
    bytes[0xffff02 - 0xf00000] = 0xf0;
    bytes[0xffff03 - 0xf00000] = 0;
    return bytes;
}
void machines_and_states()
{
    sharp::Emulator session;
    check(!session.has_rom() && session.framebuffer().size() == 132 * 64, "native startup without ROM");
    session.load_rom(sc_rom());
    check(session.info().id == "el92xx" && session.rom().version() == "1.11" && session.info().ram_base == 0x18000,
          "SC family detection");
    check(session.cpu_state().pc == 0xc0100, "SC backend reset");
    session.write8(0xc0100, 0xff);
    check(session.peek8(0xc0100) == 0xde, "ROM is immutable");
    session.write8(0x18000, 0xab);
    session.write8(0x1ffff, 0xcd);
    session.write_internal8(0xfd, 1);
    session.write8(0x61df, 1);
    session.write8(0x405f, 0x80);
    check(session.framebuffer().size() == 96 * 64 && session.framebuffer()[0] == 255 &&
              session.framebuffer()[63 * 96] == 255,
          "LCD bank, column and row orientation");
    session.write_internal8(0xf0, 0x40);
    session.set_key(el9950::Key::clear, true);
    check(session.read_internal8(0xf2) == 4, "CL KO6/KI2");
    session.write_internal8(0xf0, 0xbf);
    check(session.read_internal8(0xf2) == 0, "CL inactive strobe");
    session.set_matrix_key(8, 7, true);
    session.write_internal8(0xf1, 1);
    check(session.read_internal8(0xf2) == 128, "raw matrix high byte");
    session.set_operating_battery_percent(42);
    session.set_backup_battery_healthy(false);
    session.add_breakpoint(0xc0100);
    check(session.run_cycles(100) == 0 && session.breakpoint_hit(), "breakpoint before execution");
    session.remove_breakpoint(0xc0100);
    session.run_cycles(3);
    const auto state = session.save_state();
    session.reset();
    session.write8(0x18000, 0);
    session.load_state(state);
    check(session.save_state() == state && session.peek8(0x18000) == 0xab, "SC deterministic state round trip");
    auto damaged = state;
    damaged[100] ^= 1;
    rejects([&] { session.load_state(damaged); });
    check(session.save_state() == state, "bad CRC is transactional");
    damaged = state;
    damaged[damaged.size() - 7] = 101;
    const auto crc = el9950::crc32(std::span(damaged).first(damaged.size() - 4));
    for (unsigned n = 0; n < 4; ++n)
        damaged[damaged.size() - 4 + n] = crc >> (8 * n);
    rejects([&] { session.load_state(damaged); });
    check(session.save_state() == state, "invalid payload is transactional");
    rejects([&] { session.load_rom(std::vector<std::uint8_t>(123)); });
    check(session.save_state() == state, "failed ROM switch preserves machine");
    rejects([&] { session.set_keyboard_side(el9950::KeyboardSide::basic); });
    rejects([&] { session.set_matrix_key(16, 0, true); });
    rejects([&] { session.set_irq_line(1, true); });
    session.load_rom(sc_rom(true));
    check(session.info().ram_base == 0x1e000, "EL-9200 8 KiB profile");
    rejects([&] { session.load_state(state); });
    auto tlcs = tlcs_rom();
    el9950::Emulator legacy;
    legacy.load_rom(tlcs);
    session.load_rom(tlcs);
    check(session.info().id == "el99xx" && session.framebuffer().size() == 132 * 64, "switch to TLCS backend");
    check(session.save_state() == legacy.save_state(), "legacy EL-99xx snapshot bytes preserved");
    const auto old_state = session.save_state();
    rejects([&] { session.load_state(state); });
    check(session.save_state() == old_state, "cross-family state rejection");
    rejects([&] { session.set_matrix_key(0, 0, true); });
    session.load_rom(sc_rom());
    rejects([&] { session.load_state(old_state); });
}

void held_panel_keys()
{
    sharp::Emulator session;
    check(!session.key_pressed("second"), "unloaded key query");
    session.load_rom(sc_rom());
    session.set_key(el9950::Key::second, true);
    session.set_key("setup", true);
    session.set_key("calc", true);
    check(session.key_pressed("second") && session.key_pressed("setup") && session.key_pressed("calc"),
          "common and panel-only keys can be held together");
    session.write_internal8(0xf0, 0x40);
    session.write_internal8(0xf1, 0);
    check(session.read_internal8(0xf2) == 0xa0, "held 2ndF and SETUP reach the keyboard matrix");
    session.set_reset_contact(true);
    session.set_reset_contact(false);
    check(session.key_pressed("second") && session.key_pressed("setup") && session.key_pressed("calc"),
          "reset contact preserves held panel chord");
    const auto held = session.save_state();
    session.set_key(el9950::Key::second, false);
    session.set_key("setup", false);
    session.set_key("calc", false);
    check(!session.key_pressed("second") && !session.key_pressed("setup") && !session.key_pressed("calc"),
          "held keys can be released independently");
    session.load_state(held);
    check(session.key_pressed("second") && session.key_pressed("setup") && session.key_pressed("calc"),
          "snapshot restores common and named held keys");
    rejects([&] { session.key_pressed("invalid-key"); });
    session.load_rom(tlcs_rom());
    session.set_key("second", true);
    check(session.key_pressed("second"), "legacy backend key query");
    rejects([&] { session.key_pressed("setup"); });
}

void lcd_annunciators()
{
    using Flag = sharp::LcdAnnunciator;
    sharp::Emulator emulator;
    check(emulator.lcd_annunciators() == 0, "no LCD annunciators without a ROM");
    emulator.load_rom(sc_rom(true));
    emulator.write_internal8(0xfd, 1);
    constexpr Flag first[]{Flag::right, Flag::left, Flag::second, Flag::insert, Flag::up};
    constexpr Flag second[]{Flag::down, {}, Flag::busy, Flag::alpha, Flag::battery};
    for (const auto address : {0x61e0, 0x61e1})
    {
        const auto expected = address == 0x61e0 ? std::span<const Flag>(first) : std::span<const Flag>(second);
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            emulator.write8(address, std::uint8_t(1U << bit));
            check(emulator.lcd_annunciators() == (bit < expected.size() ? std::uint16_t(expected[bit]) : 0),
                  "each LCD RAM bit drives only its mapped annunciator");
        }
        emulator.write8(address, 0);
    }
    emulator.write8(0x61e0, 0xff);
    emulator.write8(0x61e1, 0xff);
    check(emulator.lcd_annunciators() == 0x1ff, "all nine LCD annunciators can light simultaneously");
    check(std::ranges::all_of(emulator.framebuffer(), [](auto pixel) { return pixel == 0; }),
          "annunciator writes leave the 96 x 64 dot matrix alone");
    const auto state = emulator.save_state();
    emulator.write_internal8(0xfd, 0);
    check(emulator.lcd_annunciators() == 0, "display disable hides all indicators");
    emulator.load_state(state);
    check(emulator.lcd_annunciators() == 0x1ff, "saved LCD RAM restores the indicators");
    emulator.load_rom(tlcs_rom());
    check(emulator.lcd_annunciators() == 0, "EL-99xx has no separate LCD annunciators");
}

void el9300_skin_keys(sharp::Emulator& emulator, bool real_rom)
{
    const auto skin = el9950::skin_for(emulator.rom().model(), emulator.keyboard_side());
    // F8378 firmware key codes in the same physical order as the skin.
    // ON is special: it uses the dedicated FF.3 input.
    constexpr std::uint8_t codes[]{
        0x70, 0x71, 0x72, 0x73, 0,
        0x60, 0x61, 0x62, 0x68, 0x69, 0x6a,
        0x1c, 0x1e, 0x1f, 0x1d,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
        0x46, 0x47, 0x48, 0x49, 0x4a, 0x3a,
        '7', '8', '9', '(', ')', '4', '5', '6', '*', '/',
        '1', '2', '3', '+', '-', '0', '.', '\'', 0x0d,
    };
    check(skin.image == "SHARP_EL-9300.webp" && skin.keys.size() == std::size(codes), "EL-9300 skin selected");
    check(!el9950::skin_key_at(skin, 440, 315) && !el9950::skin_key_at(skin, 627, 795),
          "LCD and gap between arrow keys are not hit regions");
    for (std::size_t index = 0; index < skin.keys.size(); ++index)
    {
        const auto& rect = skin.keys[index];
        const auto key = el9950::skin_key_at(skin, rect.x + rect.w / 2, rect.y + rect.h / 2);
        check(key == rect.key, "EL-9300 keycap center hits the mapped key");
        const auto set = [&](bool down) {
            std::visit([&](auto id) { emulator.set_key(id, down); }, *key);
        };
        set(true);
        unsigned contacts = 0;
        for (unsigned output = 0; output < 16; ++output)
        {
            emulator.write_internal8(0xf0, std::uint8_t(1u << output));
            emulator.write_internal8(0xf1, std::uint8_t((1u << output) >> 8));
            const auto inputs = emulator.read_internal8(0xf2);
            for (unsigned input = 0; input < 8; ++input)
                if (inputs & (1u << input))
                {
                    ++contacts;
                    if (real_rom)
                        check(emulator.peek8(0xf8378 + output * 8 + input) == codes[index],
                              "EL-9300 keycap produces its firmware key code");
                }
        }
        check(contacts == (codes[index] ? 1u : 0u), "EL-9300 keycap asserts one matrix contact or dedicated ON");
        check(bool(emulator.read_internal8(0xff) & 8) == (codes[index] == 0), "EL-9300 dedicated ON input");
        set(false);
    }
    emulator.set_key("edit", true);
    check(emulator.key_pressed("eqtn"), "legacy edit ID operates EQTN");
    emulator.set_key("eqtn", false);
    check(!emulator.key_pressed("edit"), "EQTN releases the legacy edit ID");
}

void optional_el9300()
{
    const auto *path = std::getenv("EL9300_ROM");
    if (!path)
        return;
    sharp::Emulator emulator;
    emulator.load_rom(std::filesystem::path(path));
    check(emulator.rom().sha256_hex() == "050375c45a00db09fd77008a4de3ca02b8bc0c0af06f8593f886f143859f6c86",
          "unexpected EL-9300 test firmware");
    emulator.run_cycles(1'000'000);
    const auto prompt = el9950::hex_sha256(el9950::sha256(emulator.framebuffer()));
    check(prompt == "d6c309df87670df044f4c5dbd3908ebb3c71b1b17c2ccbaecedad20c9bf3eac2",
          "real EL-9300 initialization prompt");
    emulator.set_key(el9950::Key::clear, true);
    emulator.run_cycles(100'000);
    emulator.set_key(el9950::Key::clear, false);
    emulator.run_cycles(1'000'000);
    check(el9950::hex_sha256(el9950::sha256(emulator.framebuffer())) != prompt, "CL advances firmware initialization");
    auto tap = [&](el9950::Key key, std::uint64_t after = 500000)
    {
        emulator.set_key(key, true);
        emulator.run_cycles(100000);
        emulator.set_key(key, false);
        emulator.run_cycles(after);
    };
    tap(el9950::Key::enter, 1000000);
    tap(el9950::Key::n1);
    tap(el9950::Key::plus);
    tap(el9950::Key::n2);
    tap(el9950::Key::enter);
    emulator.run_cycles(10000000);
    check(el9950::hex_sha256(el9950::sha256(emulator.framebuffer())) ==
              "fbfd6923173e0c9085dbc87b3cd42b7a7d9345e96a3ed08e148985e33406ca91",
          "EL-9300 real firmware 1+2=3");
    const auto state = emulator.save_state();
    emulator.run_cycles(10000);
    const auto next = emulator.save_state();
    emulator.load_state(state);
    emulator.run_cycles(10000);
    check(emulator.save_state() == next, "real ROM deterministic restore and continuation");
    using Flag = sharp::LcdAnnunciator;
    const auto flags = [](std::initializer_list<Flag> indicators) {
        std::uint16_t mask = 0;
        for (const auto indicator : indicators) mask |= std::uint16_t(indicator);
        return mask;
    };
    tap(el9950::Key::second);
    check(emulator.lcd_annunciators() == flags({Flag::second}), "real ROM lights 2ndF");
    tap(el9950::Key::second);
    tap(el9950::Key::alpha);
    check(emulator.lcd_annunciators() == flags({Flag::alpha}), "real ROM lights ALPHA");

    // Start the firmware's LCD check and compare its alternating segment groups
    // with the EL-9200 photos. The first pair uses checkerboards and the next
    // pair uses the same segment masks with diagonal dot-matrix patterns.
    emulator.load_rom(std::filesystem::path(path));
    emulator.run_cycles(1'000'000);
    tap(el9950::Key::clear, 600'000);
    tap(el9950::Key::enter, 600'000);
    emulator.set_key(el9950::Key::second, true);
    emulator.set_key(el9950::Key::enter, true);
    emulator.set_reset_contact(true);
    emulator.set_reset_contact(false);
    emulator.run_cycles(1'000'000);
    emulator.set_key(el9950::Key::second, false);
    emulator.set_key(el9950::Key::enter, false);
    emulator.run_cycles(600'000);
    tap(el9950::Key::n3, 600'000); // diag (select)
    tap(el9950::Key::n1, 600'000); // LCD check
    const auto even = flags({Flag::second, Flag::alpha, Flag::up, Flag::right});
    const auto odd = flags({Flag::left, Flag::busy, Flag::insert, Flag::battery, Flag::down});
    for (const auto phase : {even, odd, even, odd})
    {
        check(emulator.lcd_annunciators() == phase, "real ROM LCD diagnostic segment group");
        tap(el9950::Key::enter, 600'000);
    }

    emulator.load_rom(std::filesystem::path(path));
    el9300_skin_keys(emulator, true);
    std::cout << "EL-9300 1.11 initialization, 52 skin keys, LCD diagnostics and 1+2=3 passed\n";
}
} // namespace
int main()
{
    try
    {
        cpu_addressing_and_flags();
        cpu_calls_interrupts_and_power();
        opcode_decode_consistency();
        machines_and_states();
        held_panel_keys();
        lcd_annunciators();
        sharp::Emulator emulator;
        emulator.load_rom(sc_rom());
        el9300_skin_keys(emulator, false);
        optional_el9300();
    }
    catch (const std::exception &e)
    {
        std::cerr << "machine tests: " << e.what() << '\n';
        return 1;
    }
    std::cout << "machine and SC62015 tests passed\n";
}
