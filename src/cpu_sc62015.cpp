#include "sc62015.hpp"
#include "sharp/sc62015.hpp"
#include <stdexcept>
namespace sharp
{
Sc62015::Sc62015(Sc62015Bus &bus) : bus_(bus), core_(std::make_unique<pock::Cpu>(bus)) {}
Sc62015::~Sc62015() = default;
void Sc62015::reset()
{
    // A hardware reset retains general registers, flags and internal RAM.
    // A newly constructed core is already zero-initialized for cold start.
    core_->imem[0xfe] &= 0x7f;
    core_->imem[0xf7] = 0;
    core_->imem[0xf8] = (core_->imem[0xf8] & 0xd8) | 0x18;
    core_->imem[0xfb] = 0;
    core_->imem[0xfd] = 0;
    core_->imem[0xff] &= 0xfb;
    core_->halt = core_->off = false;
    core_->main_timer = core_->sub_timer = 0;
    core_->pre_1 = core_->pre_2 = 1;
    core_->reg.x.p = core_->read20(0xffffd);
}
std::uint32_t Sc62015::step() { return core_->step(); }
Sc62015State Sc62015::state() const
{
    Sc62015State s;
    const auto &r = core_->reg.x;
    s.pc = r.p;
    s.x = r.x;
    s.y = r.y;
    s.u = r.u;
    s.s = r.s;
    s.ba = r.ba;
    s.i = r.i;
    s.flags = r.f & 3;
    s.internal = core_->imem;
    s.main_timer = core_->main_timer;
    s.sub_timer = core_->sub_timer;
    s.halted = core_->halt;
    s.off = core_->off;
    return s;
}
void Sc62015::set_state(const Sc62015State &s)
{
    if ((s.pc | s.x | s.y | s.u | s.s) > address_mask || s.flags > 3 || (s.off && !s.halted) ||
        s.main_timer >= states_per_second / 500 || s.sub_timer >= states_per_second / 2)
        throw std::runtime_error("invalid SC62015 CPU state");
    auto &r = core_->reg.x;
    r.p = s.pc;
    r.x = s.x;
    r.y = s.y;
    r.u = s.u;
    r.s = s.s;
    r.ba = s.ba;
    r.i = s.i;
    r.f = s.flags;
    core_->imem = s.internal;
    core_->main_timer = s.main_timer;
    core_->sub_timer = s.sub_timer;
    core_->halt = s.halted;
    core_->off = s.off;
    core_->pre_1 = core_->pre_2 = 1;
}
void Sc62015::request_interrupt(std::uint8_t sources) { core_->imem[0xfc] |= sources & 0x7f; }
std::uint8_t Sc62015::read_internal(std::uint8_t address) const { return core_->read_internal(address, 1); }
void Sc62015::write_internal(std::uint8_t address, std::uint8_t value) { core_->write_internal(address, value, 1); }
} // namespace sharp
