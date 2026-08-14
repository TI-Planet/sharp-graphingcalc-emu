#include "el9950/cpu.hpp"
#include "el9950/constants.hpp"

#include "emu.h"
#include "tlcs900.h"

#include <algorithm>
#include <stdexcept>

namespace el9950
{
    namespace
    {
        const machine_config& standalone_config() {
            static const machine_config config;
            return config;
        }

        class Tlcs900L1 final : public tlcs900h_device, public Cpu
        {
        public:
            explicit
            Tlcs900L1(CpuBus& bus)
            : tlcs900h_device(standalone_config(), 0, "tlcs900-l1", nullptr, kClockHz), bus_(bus) {
                set_program_callbacks(
                    [this](std::uint32_t address) { return bus_.read8(address); },
                    [this](std::uint32_t address, std::uint8_t value) { bus_.write8(address, value); });
                device_start();
                std::fill(std::begin(m_level), std::end(m_level), 0);
            }

            void
            reset() override { tlcs900h_device::device_reset(); }

            std::uint32_t
            execute(std::uint32_t cycle_budget) override {
                if (!cycle_budget)
                    return 0;
                m_icount = static_cast<int>(std::min<std::uint32_t>(cycle_budget, 0x7fff'ffff));
                const auto initial = m_icount;
                execute_run();
                return static_cast<std::uint32_t>(initial - m_icount);
            }

            CpuState
            state() const override {
                CpuState s;
                s.pc = m_pc.d;
                s.sr = m_sr.d;
                for (std::size_t bank = 0; bank < 4; ++bank) {
                    s.xwa_xbc_xde_xhl[bank * 4 + 0] = m_xwa[bank].d;
                    s.xwa_xbc_xde_xhl[bank * 4 + 1] = m_xbc[bank].d;
                    s.xwa_xbc_xde_xhl[bank * 4 + 2] = m_xde[bank].d;
                    s.xwa_xbc_xde_xhl[bank * 4 + 3] = m_xhl[bank].d;
                    s.dma[bank * 4 + 0] = m_dmas[bank].d;
                    s.dma[bank * 4 + 1] = m_dmad[bank].d;
                    s.dma[bank * 4 + 2] = m_dmac[bank].d;
                    s.dma[bank * 4 + 3] = m_dmam[bank].d;
                }
                s.xix = m_xix.d;
                s.xiy = m_xiy.d;
                s.xiz = m_xiz.d;
                s.xnsp = m_xnsp.d;
                s.xssp = m_xssp.d;
                s.register_bank = static_cast<std::uint8_t>(m_regbank);
                s.halted = m_halted != 0;
                return s;
            }

            void
            set_state(const CpuState& s) override {
                m_pc.d = s.pc;
                m_sr.d = s.sr;
                for (std::size_t bank = 0; bank < 4; ++bank) {
                    m_xwa[bank].d = s.xwa_xbc_xde_xhl[bank * 4 + 0];
                    m_xbc[bank].d = s.xwa_xbc_xde_xhl[bank * 4 + 1];
                    m_xde[bank].d = s.xwa_xbc_xde_xhl[bank * 4 + 2];
                    m_xhl[bank].d = s.xwa_xbc_xde_xhl[bank * 4 + 3];
                    m_dmas[bank].d = s.dma[bank * 4 + 0];
                    m_dmad[bank].d = s.dma[bank * 4 + 1];
                    m_dmac[bank].d = s.dma[bank * 4 + 2];
                    m_dmam[bank].d = s.dma[bank * 4 + 3];
                }
                m_xix.d = s.xix;
                m_xiy.d = s.xiy;
                m_xiz.d = s.xiz;
                m_xnsp.d = s.xnsp;
                m_xssp.d = s.xssp;
                m_regbank = s.register_bank & 3;
                m_halted = s.halted;
                m_prefetch_clear = true;
            }

            void
            set_irq(std::uint8_t line, bool asserted) override {
                if (line >= TLCS900_NUM_INPUTS)
                    return;
                m_level[line] = asserted;
                m_check_irqs = 1;
                if (asserted)
                    m_halted = 0;
            }

        private:
            void
            tlcs900_check_hdma() override {}

            void
            tlcs900_check_irqs() override {
                // TMP91C025 interrupt routing used by the calculator. MAME input numbers
                // and MCU interrupt-source numbers are separate namespaces. Priority 7 is
                // reserved/disabled.
                struct Candidate
                {
                    std::uint8_t line, source, priority, vector;
                };
                const auto inte12 = bus_.read8(0x0091);
                const auto inte38 = bus_.read8(0x0092);
                const auto inte45 = bus_.read8(0x0093);
                const auto inte67 = bus_.read8(0x0094);
                const auto inte9 = bus_.read8(0x0095);
                const Candidate candidates[]{
                    {TLCS900_INT1, 11, static_cast<std::uint8_t>(inte12 & 7), 0x2c},
                    {TLCS900_INT2, 12, static_cast<std::uint8_t>((inte12 >> 4) & 7), 0x30},
                    {TLCS900_INT3, 13, static_cast<std::uint8_t>(inte38 & 7), 0x34},
                    {TLCS900_INT4, 14, static_cast<std::uint8_t>(inte45 & 7), 0x38},
                    {TLCS900_INT5, 15, static_cast<std::uint8_t>((inte45 >> 4) & 7), 0x3c},
                    {TLCS900_INT6, 16, static_cast<std::uint8_t>(inte67 & 7), 0x40},
                    {TLCS900_INT7, 17, static_cast<std::uint8_t>((inte67 >> 4) & 7), 0x44},
                    {TLCS900_INT8, 18, static_cast<std::uint8_t>((inte38 >> 4) & 7), 0x48},
                    {TLCS900_INT9, 19, static_cast<std::uint8_t>(inte9 & 7), 0x4c},
                    // The calculator keyboard matrix has its own one-shot INTKEY source,
                    // using vector FFFF70 and the high nibble of IIMC register 0x97.
                    {0xff, 28, static_cast<std::uint8_t>((bus_.read8(0x0097) >> 4) & 7), 0x70},
                };
                const auto current_mask = static_cast<std::uint8_t>((m_sr.b.h >> 4) & 7);
                const Candidate* selected = nullptr;
                for (const auto& candidate : candidates)
                    if ((candidate.line == 0xff
                             ? bus_.interrupt_pending(candidate.source)
                             : (m_level[candidate.line] || bus_.interrupt_pending(candidate.source))) &&
                        candidate.priority > 0 && candidate.priority >= current_mask &&
                        candidate.priority < 7 && (!selected || candidate.priority >= selected->priority))
                        selected = &candidate;
                if (!selected)
                    return;
                bus_.acknowledge_interrupt(selected->source);
                m_xssp.d -= 4;
                WRMEML(m_xssp.d, m_pc.d);
                m_xssp.d -= 2;
                WRMEMW(m_xssp.d, m_sr.w.l);
                m_sr.b.h = (m_sr.b.h & 0x8f) | ((selected->priority + 1) << 4);
                m_pc.d = RDMEML(0xffff00 + selected->vector);
                m_cycles += 18;
                m_prefetch_clear = true;
                m_halted = 0;
            }

            void
            tlcs900_handle_ad() override {}

            void
            tlcs900_handle_timers() override {
                bus_.cpu_cycles(static_cast<std::uint32_t>(m_cycles));
                if (bus_.any_interrupt_pending()) {
                    // A masked interrupt still releases HALT on TLCS-900. Firmware uses
                    // EI 7; HALT as a sleep gate, then briefly lowers IFF to dispatch the
                    // pending source after performing its wake-up bookkeeping.
                    m_halted = 0;
                    m_check_irqs = 1;
                }
            }

            CpuBus& bus_;
        };
    }

    std::unique_ptr<Cpu> make_tlcs900_l1_cpu(CpuBus& bus) { return std::make_unique<Tlcs900L1>(bus); }
} // namespace el9950
