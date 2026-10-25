#include "el9950/emulator.hpp"
#include "el9950/flash.hpp"
#include "el9950/lcd.hpp"
#include "el9950/sha256.hpp"
#include "el9950/skin.hpp"
#include "el9950/state.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr "\n"; ++failures; } } while (0)

void test_sha256() {
  const std::string abc = "abc";
  const auto bytes = std::span(reinterpret_cast<const std::uint8_t *>(abc.data()), abc.size());
  CHECK(el9950::hex_sha256(el9950::sha256(bytes)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void test_lcd() {
  el9950::Lcd lcd; lcd.reset(); lcd.command(0xaf); lcd.command(0xb0); lcd.command(0x00); lcd.command(0x10); lcd.data(0x81);
  const auto &fb = lcd.framebuffer();
  CHECK(fb[0] == 0xff); CHECK(fb[7 * el9950::kLcdWidth] == 0xff); CHECK(fb[el9950::kLcdWidth] == 0);
  lcd.command(0xa7); CHECK(lcd.framebuffer()[el9950::kLcdWidth] == 0xff);
  lcd.command(0xa6); lcd.command(0xc0); CHECK(lcd.framebuffer()[63 * el9950::kLcdWidth] == 0xff);
}

void unlock_program(el9950::Flash &flash, std::uint32_t offset, std::uint8_t value) {
  flash.write(0x555,0xaa); flash.write(0x2aa,0x55); flash.write(0x555,0xa0); flash.write(offset,value);
}
void test_flash() {
  std::vector<std::uint8_t> rom(el9950::kFlashSize,0xff); rom[0x1234]=0xf3;
  el9950::Flash flash; flash.load(rom); unlock_program(flash,0x1234,0x5a); CHECK(flash.read(0x1234)==0x52); CHECK(rom[0x1234]==0xf3);
  bool checkpoint=false; flash.set_boot_write_callback([&]{ checkpoint=true; });
  unlock_program(flash,0xfc000,0x00); CHECK(checkpoint); CHECK(flash.read(0xfc000)==0x00);
}

void test_keyboard_and_link() {
  el9950::Keyboard keyboard; keyboard.set(el9950::Key::second,true); keyboard.set(el9950::Key::variable,true);
  CHECK(keyboard.pressed(el9950::Key::second)); CHECK(el9950::Keyboard::coordinate(el9950::Key::second).raw_code==0xd8);
  CHECK(el9950::Keyboard::id(el9950::Key::second)=="second");
  CHECK(el9950::Keyboard::from_id("variable")==el9950::Key::variable);
  CHECK(!el9950::Keyboard::from_id("not-a-key"));
  const auto second=el9950::Keyboard::coordinate(el9950::Key::second);
  CHECK(!(keyboard.read_inputs(static_cast<std::uint8_t>(~(1U<<second.output))) & (1U<<second.input)));

  using K=el9950::Key;
  struct ExpectedCoordinate { K key; std::uint8_t output,input; };
  constexpr ExpectedCoordinate expected[]{
    {K::y_equals,0,5},{K::graph,0,4},{K::table,0,3},{K::window,0,2},{K::zoom,0,1},{K::trace,0,0},
    {K::option,0,6},{K::stat_plot,0,7},{K::slide_show,1,4},{K::up,1,1},{K::left,1,3},{K::right,1,0},{K::down,1,2},
    {K::on,7,7},{K::second,1,6},{K::alpha,1,5},{K::math,2,6},{K::stat,2,5},{K::prgm,2,4},{K::del,2,3},
    {K::backspace,2,2},{K::clear,2,1},{K::sin,2,7},{K::cos,3,5},{K::tan,3,4},{K::log,3,3},{K::ln,3,2},
    {K::square,2,0},{K::fraction,3,7},{K::mixed_fraction,3,6},{K::power,4,4},{K::comma,4,2},{K::sto,3,1},
    {K::variable,3,0},{K::n7,4,6},{K::n8,4,5},{K::n9,4,3},{K::lparen,4,1},{K::rparen,4,0},{K::n4,5,7},
    {K::n5,5,6},{K::n6,5,4},{K::multiply,5,2},{K::divide,5,0},{K::n1,6,6},{K::n2,5,5},{K::n3,5,3},
    {K::plus,5,1},{K::minus,6,0},{K::n0,6,5},{K::decimal,6,4},{K::negative,6,3},{K::exp,6,2},{K::enter,6,1}
  };
  CHECK(std::size(expected)==static_cast<std::size_t>(K::count));
  for(const auto &[key,output,input]:expected) {
    const auto coordinate=el9950::Keyboard::coordinate(key);
    CHECK(coordinate.output==output); CHECK(coordinate.input==input);
    if(output<7) CHECK(coordinate.raw_code==static_cast<std::uint8_t>(0x17U+output+0x20U*input));
  }
  el9950::LinkPort a,b; el9950::connect(a,b); CHECK(a.sample(1)); b.drive(1,false); CHECK(!a.sample(1));
}

void test_keyboard_ports() {
  el9950::Keyboard keyboard; el9950::LinkPort link;
  el9950::Tmp91c025 mcu(keyboard, link); mcu.reset();
  keyboard.set(el9950::Key::second, true); // output 1, input 6
  mcu.write(0x06, 0xb0); mcu.write(0x13, 0x0e); // output 1 active
  CHECK((mcu.read(0x19) & 0x40) == 0);
  mcu.write(0x06, 0xf0); // no keyboard output active
  CHECK(mcu.read(0x19) == 0xff);
}

void test_advanced_keyboard_input() {
  el9950::Keyboard keyboard; el9950::LinkPort link;
  el9950::Tmp91c025 mcu(keyboard, link); mcu.reset();
  CHECK(mcu.read(0x29) == 0x40);
  // P4.6 is an external input: firmware output writes must not change it.
  for(unsigned value=0;value<256;++value) {
    mcu.write(0x29,static_cast<std::uint8_t>(value));
    CHECK(mcu.read(0x29) == (value | 0x40));
  }
  // The keyboard side is independent of the operating battery setting.
  for(const auto percent:{0,10,100}) {
    mcu.set_operating_battery_percent(static_cast<std::uint8_t>(percent));
    mcu.write(0x29,0); CHECK(mcu.read(0x29) == 0x40);
  }
  auto regs=mcu.registers(); regs[0x29]=0;
  mcu.restore_registers(regs); CHECK(mcu.read(0x29) == 0x40);
  mcu.reset(); CHECK(mcu.read(0x29) == 0x40);
  keyboard.set_side(el9950::KeyboardSide::basic);
  for (unsigned value=0; value<256; ++value) {
    mcu.write(0x29,static_cast<std::uint8_t>(value));
    CHECK(mcu.read(0x29) == (value & ~0x40U));
  }
  mcu.reset(); CHECK((mcu.read(0x29)&0x40)==0);
  keyboard.reset(); CHECK(keyboard.side()==el9950::KeyboardSide::basic);
}

void test_on_input() {
  el9950::Keyboard keyboard;
  el9950::LinkPort link;
  el9950::Tmp91c025 mcu(keyboard, link);
  mcu.reset();
  CHECK(mcu.read(0x22) == 0x1a);
  keyboard.set(el9950::Key::on, true);
  CHECK((mcu.read(0x22) & 0x10) == 0);
  keyboard.set(el9950::Key::on, false);
  CHECK(mcu.read(0x22) == 0x1a);
  mcu.set_backup_battery_healthy(false); CHECK(mcu.read(0x22) == 0x18);
  mcu.request_interrupt(28); CHECK(mcu.any_interrupt_pending()); CHECK(mcu.interrupt_pending(28));
  mcu.acknowledge_interrupt(28); CHECK(!mcu.any_interrupt_pending());
}

std::vector<std::uint8_t> test_rom() {
  std::vector<std::uint8_t> rom(el9950::kFlashSize,0xff);
  rom[0]=0x00; // NOP
  rom[0x0fff00]=0x00; rom[0x0fff01]=0x00; rom[0x0fff02]=0xf0;
  return rom;
}
std::vector<std::uint8_t> model_rom(std::string_view banner, unsigned major, unsigned minor) {
  auto bytes=test_rom();
  bytes[0x35813]=0x43;
  bytes[0x35814]=0x00; bytes[0x35815]=0x00; bytes[0x35816]=0xf9; bytes[0x35817]=0;
  std::copy(banner.begin(),banner.end(),bytes.begin()+0x90000);
  bytes[0xfbfe4]=major; bytes[0xfbfe5]=minor;
  return bytes;
}

void test_models_and_sides() {
  using M=el9950::Model; using S=el9950::KeyboardSide;
  struct Case { std::string_view banner; unsigned major,minor; M model; std::string_view version; };
  const Case cases[]{
    {"EL\x8a" "9900       Ver  !  ",1,0x09,M::el9900,"1.09"},
    {"EL\x8a" "9900G      Ver  !  ",3,0x01,M::el9900g,"3.01"},
    {"EL\x8a" "9900G SII  Ver  !  ",4,0x10,M::el9900g_sii,"4.10"},
    {"EL\x8a" "9950       Ver  !  ",4,0x11,M::el9900g_sii,"4.11"},
    {"EL\x8a" "9950       Ver  !  ",1,0x20,M::el9950,"1.20"},
    {"EL\x8a" "9950       Ver  !  ",1,0x21,M::el9950,"1.21"}
  };
  for (const auto& c:cases) {
    auto bytes=model_rom(c.banner,c.major,c.minor);
    bytes[0x70000]=0x42; // User data does not change the model.
    el9950::Emulator emu; emu.load_rom(bytes);
    CHECK(emu.rom().model()==c.model); CHECK(emu.rom().version()==c.version);
    CHECK(emu.keyboard_side()==S::advanced);
    if (el9950::has_reversible_keyboard(c.model)) {
      emu.set_keyboard_side(S::basic); CHECK((emu.bus().read8(0x29)&0x40)==0);
      const auto state=emu.save_state();
      auto invalid=state; invalid[invalid.size()-5]=2;
      const auto invalid_crc=el9950::crc32(std::span(invalid).first(invalid.size()-4));
      for(unsigned i=0;i<4;++i) invalid[invalid.size()-4+i]=(invalid_crc>>(8*i))&0xff;
      bool rejected=false; try { emu.load_state(invalid); } catch(const std::exception&) { rejected=true; }
      CHECK(rejected); CHECK(emu.save_state()==state);
      emu.set_keyboard_side(S::advanced); emu.load_state(state);
      CHECK(emu.keyboard_side()==S::basic); CHECK(emu.save_state()==state);
      emu.reset(); CHECK(emu.keyboard_side()==S::basic);
      // v6 snapshots have no side byte and retain the previous advanced default.
      auto legacy=state; legacy.erase(legacy.end()-5); legacy[8]=6;
      const auto crc=el9950::crc32(std::span(legacy).first(legacy.size()-4));
      for (unsigned i=0;i<4;++i) legacy[legacy.size()-4+i]=(crc>>(8*i))&0xff;
      emu.load_state(legacy); CHECK(emu.keyboard_side()==S::advanced);
      emu.set_keyboard_side(S::basic); emu.load_rom(bytes); CHECK(emu.keyboard_side()==S::advanced);
    } else {
      bool rejected=false; try { emu.set_keyboard_side(S::basic); } catch (const std::exception&) { rejected=true; }
      CHECK(rejected); CHECK(emu.keyboard_side()==S::advanced);
    }
    for (const auto side:{S::advanced,S::basic}) {
      const auto skin=el9950::skin_for(c.model,side);
      CHECK(skin.keys.size()==static_cast<unsigned>(el9950::Key::count));
      for (const auto& r:skin.keys) {
        CHECK(el9950::skin_key_at(skin,r.x+r.w/2,r.y+r.h/2)==r.key);
        CHECK(r.x>=0 && r.y>=0 && r.x+r.w<=skin.width && r.y+r.h<=skin.height);
      }
      CHECK(!el9950::skin_key_at(skin,0,0));
      CHECK(!el9950::skin_key_at(skin,skin.lcd_x+10,skin.lcd_y+10));
    }
  }
  CHECK(el9950::RomImage::from_bytes(test_rom()).model()==M::unknown);
  auto invalid=model_rom(cases[0].banner,1,0xfa);
  CHECK(el9950::RomImage::from_bytes(invalid).model()==M::unknown);
  invalid=model_rom(cases[0].banner,1,0x09); invalid[0x35817]=0xff;
  CHECK(el9950::RomImage::from_bytes(invalid).model()==M::unknown);
}

void test_cpu_and_state() {
  el9950::Emulator emu; emu.load_rom(test_rom()); CHECK(emu.cpu_state().pc==0xf00000);
  emu.set_key(el9950::Key::second,true); emu.set_operating_battery_percent(42); emu.set_backup_battery_healthy(false);
  const auto ran=emu.run_cycles(32); CHECK(ran>=32);
  const auto state=emu.save_state(); const auto before=emu.cpu_state(); emu.run_cycles(32); emu.set_key(el9950::Key::second,false);
  emu.load_state(state); CHECK(emu.cpu_state().pc==before.pc); CHECK(emu.cpu_state().sr==before.sr);
  CHECK(emu.keyboard().pressed(el9950::Key::second)); CHECK(emu.operating_battery_percent()==42);
  CHECK(!emu.backup_battery_healthy()); CHECK(emu.save_state()==state);
}

void test_io_observer() {
  el9950::Emulator emu; emu.load_rom(test_rom());
  std::uint64_t cycle=99; std::uint32_t address=0; std::uint8_t value=0; bool write=false;
  emu.set_io_observer([&](auto c, auto w, auto a, auto v){cycle=c;write=w;address=a;value=v;});
  emu.bus().write8(0x18,0x5a); CHECK(cycle==0); CHECK(write); CHECK(address==0x18); CHECK(value==0x5a);
  CHECK(emu.bus().read8(0x18)==0x5a); CHECK(!write); CHECK(address==0x18); CHECK(value==0x5a);
  emu.bus().write8(0x449,0xa5); CHECK(emu.bus().read8(0x449)==0xa5);
  CHECK(address==0x449); CHECK(value==0xa5);
  emu.bus().write8(0xe0,0x04); CHECK(emu.bus().read8(0xe0)&0x04);
  emu.bus().cpu_cycles(1); CHECK(!(emu.bus().read8(0xe0)&0x04));
}

void optional_real_rom() {
  const char *path=std::getenv("EL9950_ROM"); if (!path) return;
  el9950::Emulator emu; emu.load_rom(std::filesystem::path(path)); CHECK(emu.known_rom()); CHECK(emu.cpu_state().pc>=el9950::kFlashBase);
  auto run_to_halt=[&emu](std::uint64_t limit){std::uint64_t done=0;while(done<limit&&!emu.cpu_state().halted){const auto ran=emu.run_cycles(std::min<std::uint64_t>(4096,limit-done));if(!ran)break;done+=ran;}return done;};
  CHECK(run_to_halt(20'000'000)>0); CHECK(emu.cpu_state().halted); CHECK(emu.cpu_state().pc==0xffa0aa);
  emu.set_key(el9950::Key::on,true); CHECK(emu.run_cycles(500'000)>=500'000); emu.set_key(el9950::Key::on,false);
  CHECK(run_to_halt(20'000'000)>0); CHECK(emu.cpu_state().halted);
  const auto hash=el9950::hex_sha256(el9950::sha256(std::span(emu.framebuffer())));
  CHECK(hash=="54fa7f93186bae99fa3ec33bed7da127a680ef5a685180f2ef092409d0de94c9");
  std::cout << "real ROM powered-on LCD smoke: PC=0x" << std::hex << emu.cpu_state().pc << std::dec << " hash=" << hash << '\n';

  // Complete first-use initialization through the real keyboard ISR and its
  // 16 ms alarm-driven debounce, then acknowledge the confirmation screen.
  emu.run_cycles(2'000'000);
  emu.set_key(el9950::Key::clear,true); emu.run_cycles(500'000); emu.set_key(el9950::Key::clear,false);
  emu.run_cycles(3'000'000);
  CHECK(el9950::hex_sha256(el9950::sha256(std::span(emu.framebuffer())))==
        "29fd1e3206a56515f9f5a8faa55a60fb47552d72819bde8020f51cc0ed672d68");
  emu.set_key(el9950::Key::clear,true); emu.run_cycles(500'000); emu.set_key(el9950::Key::clear,false);
  emu.run_cycles(5'000'000);
  CHECK(el9950::hex_sha256(el9950::sha256(std::span(emu.framebuffer())))==
        "e4c09bff61f0e80e518ec42244e6e098ae47b9d1eada1058228c956c88c3fe6b");

  // Exercise every ordinary key through the ROM's scanner and compare the
  // translated firmware event at F37418 with the known 1.20 table. This
  // catches electrical permutations even when host bindings and skin labels
  // still appear internally consistent.
  using K=el9950::Key;
  const std::pair<K,std::uint16_t> expected_events[]{
    {K::y_equals,0x0f8b},{K::graph,0x0f87},{K::table,0x0f85},{K::window,0x0f8c},{K::zoom,0x0e25},{K::trace,0x0b40},
    {K::option,0x0f8f},{K::stat_plot,0x0e23},{K::slide_show,0x0e15},{K::up,0x0f1b},{K::left,0x0f1a},
    {K::right,0x0f19},{K::down,0x0f1c},{K::math,0x0e17},{K::stat,0x0e22},{K::prgm,0x0e13},{K::del,0x0f17},
    {K::backspace,0x0f16},{K::clear,0x0f15},{K::sin,0x8800},{K::cos,0x8900},{K::tan,0x8a00},{K::log,0x8200},
    {K::ln,0x8300},{K::square,0x7900},{K::fraction,0xfb00},{K::mixed_fraction,0x7500},{K::power,0x7600},
    {K::comma,0x5f00},{K::sto,0x7c00},{K::variable,0x1f00},{K::n7,0x3700},{K::n8,0x3800},{K::n9,0x3900},
    {K::lparen,0x2a00},{K::rparen,0x2b00},{K::n4,0x3400},{K::n5,0x3500},{K::n6,0x3600},{K::multiply,0x7000},
    {K::divide,0x7100},{K::n1,0x3100},{K::n2,0x3200},{K::n3,0x3300},{K::plus,0x7200},{K::minus,0x7300},
    {K::n0,0x3000},{K::decimal,0x2100},{K::negative,0x2300},{K::exp,0x2200},{K::enter,0x0f21}
  };
  const auto calculation_state=emu.save_state();
  emu.add_breakpoint(0xf37418);
  for(const auto &[key,event]:expected_events) {
    emu.load_state(calculation_state); emu.clear_breakpoint_hit();
    emu.set_key(key,true); emu.run_cycles(1'000'000);
    const auto cpu=emu.cpu_state();
    CHECK(emu.breakpoint_hit());
    const auto actual=static_cast<std::uint16_t>(cpu.xwa_xbc_xde_xhl[cpu.register_bank*4+1]);
    if(actual!=event) std::cerr << "key event mismatch for " << el9950::Keyboard::id(key)
                               << ": expected 0x" << std::hex << event << ", got 0x" << actual << std::dec << '\n';
    CHECK(actual==event);
  }
  emu.remove_breakpoint(0xf37418);
  emu.add_breakpoint(0xf3738d);
  const std::pair<K,std::uint8_t> modifiers[]{
    {K::second,0xd8},{K::alpha,0xb8}
  };
  for(const auto &[key,raw]:modifiers) {
    emu.load_state(calculation_state); emu.clear_breakpoint_hit();
    emu.set_key(key,true); emu.run_cycles(1'000'000);
    const auto cpu=emu.cpu_state();
    CHECK(emu.breakpoint_hit());
    CHECK((cpu.xwa_xbc_xde_xhl[cpu.register_bank*4+1]&0xffU)==raw);
  }
  emu.remove_breakpoint(0xf3738d);

  emu.load_state(calculation_state);
  emu.set_key(K::alpha,true); emu.run_cycles(500'000); emu.set_key(K::alpha,false); emu.run_cycles(3'000'000);
  const auto alpha_state=emu.save_state();
  const K alpha_letters[]{
    K::sin,K::cos,K::tan,K::log,K::ln,K::square,K::fraction,K::mixed_fraction,K::power,K::comma,K::sto,
    K::n7,K::n8,K::n9,K::lparen,K::rparen,K::n4,K::n5,K::n6,K::multiply,K::divide,K::n1,K::n2,K::n3,K::plus,K::minus
  };
  emu.add_breakpoint(0xf37418);
  for(std::size_t index=0;index<std::size(alpha_letters);++index) {
    emu.load_state(alpha_state); emu.clear_breakpoint_hit();
    emu.set_key(alpha_letters[index],true); emu.run_cycles(1'000'000);
    const auto cpu=emu.cpu_state();
    CHECK(emu.breakpoint_hit());
    const auto actual=static_cast<std::uint16_t>(cpu.xwa_xbc_xde_xhl[cpu.register_bank*4+1]);
    const auto expected=static_cast<std::uint16_t>((0x41U+index)<<8);
    if(actual!=expected) std::cerr << "ALPHA event mismatch for " << el9950::Keyboard::id(alpha_letters[index])
                                  << ": expected 0x" << std::hex << expected << ", got 0x" << actual << std::dec << '\n';
    CHECK(actual==expected);
  }
  emu.remove_breakpoint(0xf37418);

  emu.load_state(calculation_state);
  emu.set_key(K::n1,true); emu.run_cycles(500'000); emu.set_key(K::n1,false); emu.run_cycles(3'000'000);
  const auto one_frame=el9950::hex_sha256(el9950::sha256(std::span(emu.framebuffer())));
  CHECK(one_frame=="8b1dd4d80a205e73856e922efef719da8967ff210f4c4f7466521ba11abaa1b2");
  emu.set_key(K::left,true); emu.run_cycles(500'000); emu.set_key(K::left,false); emu.run_cycles(3'000'000);
  CHECK(el9950::hex_sha256(el9950::sha256(std::span(emu.framebuffer())))!=one_frame);
  emu.set_key(K::right,true); emu.run_cycles(500'000); emu.set_key(K::right,false); emu.run_cycles(3'000'000);
  CHECK(el9950::hex_sha256(el9950::sha256(std::span(emu.framebuffer())))==one_frame);
  emu.set_key(K::plus,true); emu.run_cycles(500'000); emu.set_key(K::plus,false); emu.run_cycles(3'000'000);
  emu.set_key(K::n1,true); emu.run_cycles(500'000); emu.set_key(K::n1,false); emu.run_cycles(3'000'000);
  emu.set_key(K::enter,true); emu.run_cycles(500'000); emu.set_key(K::enter,false); emu.run_cycles(5'000'000);
  CHECK(el9950::hex_sha256(el9950::sha256(std::span(emu.framebuffer())))==
        "38e275f17ed06306d3e8aff750556b637a1e0c6026386c02ea2fc4b4af9375fd");

  el9950::Emulator depleted; depleted.load_rom(std::filesystem::path(path)); depleted.set_backup_battery_healthy(false);
  auto run_depleted_to_halt=[&depleted](std::uint64_t limit){std::uint64_t done=0;while(done<limit&&!depleted.cpu_state().halted){const auto ran=depleted.run_cycles(std::min<std::uint64_t>(4096,limit-done));if(!ran)break;done+=ran;}return done;};
  CHECK(run_depleted_to_halt(20'000'000)>0); depleted.set_key(el9950::Key::on,true); CHECK(depleted.run_cycles(500'000)>=500'000); depleted.set_key(el9950::Key::on,false);
  CHECK(run_depleted_to_halt(20'000'000)>0);
  const auto depleted_hash=el9950::hex_sha256(el9950::sha256(std::span(depleted.framebuffer())));
  CHECK(depleted_hash=="5490c94fa30008b29e50e0adac725251d7337867b704254190ab353e5f613768");
}

void optional_menu_rom() {
  const char *path=std::getenv("EL99_MENU_ROM"); if(!path) return;
  el9950::Emulator emu; emu.load_rom(std::filesystem::path(path));
  using K=el9950::Key;
  auto tap=[&](K key){emu.set_key(key,true);emu.run_cycles(500'000);emu.set_key(key,false);emu.run_cycles(3'000'000);};
  auto lcd_region_hash=[&](unsigned width,unsigned height){
    std::vector<std::uint8_t> pixels;
    for(unsigned y=0;y<height;++y)
      for(unsigned x=0;x<width;++x) pixels.push_back(emu.framebuffer()[y*el9950::kLcdWidth+x]);
    return el9950::hex_sha256(el9950::sha256(pixels));
  };
  emu.run_cycles(20'000'000); tap(K::on); emu.run_cycles(20'000'000);
  tap(K::clear); tap(K::clear);
  CHECK(emu.bus().peek8(0x400d2e)==0); // Firmware's advanced-side flag.
  const auto calculation_state=emu.save_state();
  // These LCD regions match EL-9900 1.09, EL-9900G 3.01, EL-9900GS2 4.10,
  // and EL-9950 1.20. Crop labels to exclude ROM-specific stored programs.
  tap(K::prgm);
  CHECK(lcd_region_hash(42,24)=="62eeccb507accf1aecf5b9966f3385d75a06c44bda142d41a2d9c4f839ceb1e6"); // EXEC/EDIT/NEW
  emu.load_state(calculation_state); tap(K::math);
  CHECK(lcd_region_hash(42,64)=="9e2379a753e7f035dd05afcc8a8f441918f4f719824709a9738028f07d0a51e7"); // CALC through COMPLX
  emu.load_state(calculation_state); tap(K::second); tap(K::prgm);
  CHECK(lcd_region_hash(132,8)=="ac54e604d70ded502e9f2dcdf2cc40d475dc680e4b6f7e0d5dd048a09208d13b"); // SOLVER
  // Old snapshots may contain the basic-side flag and a low P4 latch.
  // EL-9950 firmware bypasses the side detector and always initializes advanced.
  if(el9950::has_reversible_keyboard(emu.rom().model())) {
    emu.load_state(calculation_state);
    emu.bus().write8(0x400d2e,1); emu.bus().write8(0x29,0);
    const auto legacy_state=emu.save_state();
    emu.reset(); emu.load_state(legacy_state); tap(K::prgm);
    CHECK(emu.bus().peek8(0x400d2e)==0);
    tap(K::clear); // Acknowledge "THE KEYBOARD AND THE MODE CHANGED".
    tap(K::prgm);
    CHECK(lcd_region_hash(42,24)=="62eeccb507accf1aecf5b9966f3385d75a06c44bda142d41a2d9c4f839ceb1e6");
  }
  if(el9950::has_reversible_keyboard(emu.rom().model())) {
    using S=el9950::KeyboardSide;
    emu.load_state(calculation_state);
    emu.set_keyboard_side(S::basic);
    CHECK((emu.bus().read8(0x29)&0x40)==0);
    tap(K::clear); // Detect the physical flip, then acknowledge the mode change.
    tap(K::clear);
    CHECK(emu.bus().peek8(0x400d2e)==1);
    const auto basic_state=emu.save_state();
    tap(K::math);
    CHECK(lcd_region_hash(42,64)!="9e2379a753e7f035dd05afcc8a8f441918f4f719824709a9738028f07d0a51e7");
    emu.load_state(basic_state);
    CHECK(emu.keyboard_side()==S::basic);
    // Shared electrical positions select the basic-side conversion functions.
    emu.add_breakpoint(0xf37418);
    const std::pair<K,std::uint16_t> basic_events[]{
      {K::sin,0x0a8c}, {K::cos,0x0a8e}, {K::tan,0x0a8f},
      {K::log,0x0a8d}, {K::ln,0x0a61}, {K::n1,0x3100}, {K::enter,0x0f21}
    };
    for(const auto& [key,event]:basic_events) {
      emu.load_state(basic_state); emu.clear_breakpoint_hit();
      emu.set_key(key,true); emu.run_cycles(1'000'000);
      CHECK(emu.breakpoint_hit());
      const auto cpu=emu.cpu_state();
      CHECK((cpu.xwa_xbc_xde_xhl[cpu.register_bank*4+1]&0xffff)==event);
    }
    emu.remove_breakpoint(0xf37418); emu.clear_breakpoint_hit();
    emu.load_state(basic_state); emu.set_keyboard_side(S::advanced);
    tap(K::clear); tap(K::clear);
    CHECK(emu.bus().peek8(0x400d2e)==0);
    tap(K::prgm);
    CHECK(lcd_region_hash(42,24)=="62eeccb507accf1aecf5b9966f3385d75a06c44bda142d41a2d9c4f839ceb1e6");
  }
  std::cout << "real ROM advanced keyboard menus: " << emu.rom().sha256_hex() << '\n';
}
}

int main() {
  try { test_sha256(); test_lcd(); test_flash(); test_keyboard_and_link(); test_keyboard_ports(); test_advanced_keyboard_input(); test_on_input(); test_cpu_and_state(); test_models_and_sides(); test_io_observer(); optional_real_rom(); optional_menu_rom(); }
  catch (const std::exception &e) { std::cerr << "unexpected exception: " << e.what() << '\n'; ++failures; }
  if (failures) { std::cerr << failures << " test(s) failed\n"; return 1; }
  std::cout << "all tests passed\n"; return 0;
}
