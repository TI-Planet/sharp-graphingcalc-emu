#include "el9950/flash.hpp"
#include "el9950/constants.hpp"

#include <algorithm>
#include <stdexcept>

namespace el9950
{
    void Flash::load(std::span<const std::uint8_t> immutable_rom) {
        if (immutable_rom.size() != kFlashSize)
            throw std::runtime_error("invalid flash image size");
        rom_.assign(immutable_rom.begin(), immutable_rom.end());
        overlay_.clear();
        reset_command_state();
        boot_checkpoint_taken_ = false;
    }

    std::uint8_t Flash::read(std::uint32_t offset) const {
        if (offset >= rom_.size())
            return 0xff;
        if (const auto it = overlay_.find(offset); it != overlay_.end())
            return it->second;
        return rom_[offset];
    }

    void Flash::before_mutation(std::uint32_t offset) {
        if (offset >= kBootPageBase - kFlashBase && !boot_checkpoint_taken_) {
            boot_checkpoint_taken_ = true;
            if (boot_write_callback_)
                boot_write_callback_();
        }
    }

    void Flash::program(std::uint32_t offset, std::uint8_t value) {
        if (offset >= rom_.size())
            return;
        before_mutation(offset);
        const auto programmed = static_cast<std::uint8_t>(read(offset) & value);
        if (programmed == rom_[offset])
            overlay_.erase(offset);
        else
            overlay_[offset] = programmed;
    }

    void Flash::erase_sector(std::uint32_t offset) {
        constexpr std::uint32_t sector_size = 0x4000;
        const auto begin = offset & ~(sector_size - 1);
        before_mutation(begin);
        for (std::uint32_t i = begin; i < std::min<std::uint32_t>(begin + sector_size, rom_.size()); ++i) {
            if (rom_[i] == 0xff)
                overlay_.erase(i);
            else
                overlay_[i] = 0xff;
        }
    }

    void Flash::write(std::uint32_t offset, std::uint8_t value) {
        if (value == 0xf0) {
            reset_command_state();
            return;
        }
        switch (command_state_) {
            case CommandState::idle:
                if ((offset & 0x7ff) == 0x555 && value == 0xaa)
                    command_state_ = CommandState::unlock1;
                break;
            case CommandState::unlock1:
                command_state_ = ((offset & 0x7ff) == 0x2aa && value == 0x55) ? CommandState::unlock2 : CommandState::idle;
                break;
            case CommandState::unlock2:
                if ((offset & 0x7ff) != 0x555) {
                    command_state_ = CommandState::idle;
                    break;
                }
                if (value == 0xa0)
                    command_state_ = CommandState::program;
                else if (value == 0x80)
                    command_state_ = CommandState::erase1;
                else
                    command_state_ = CommandState::idle;
                break;
            case CommandState::program:
                program(offset, value);
                command_state_ = CommandState::idle;
                break;
            case CommandState::erase1:
                command_state_ = ((offset & 0x7ff) == 0x555 && value == 0xaa) ? CommandState::erase2 : CommandState::idle;
                break;
            case CommandState::erase2:
                command_state_ = ((offset & 0x7ff) == 0x2aa && value == 0x55) ? CommandState::erase3 : CommandState::idle;
                break;
            case CommandState::erase3:
                if (value == 0x30)
                    erase_sector(offset);
                else if ((offset & 0x7ff) == 0x555 && value == 0x10) {
                    before_mutation(0);
                    for (std::uint32_t i = 0; i < rom_.size(); ++i)
                        if (rom_[i] != 0xff)
                            overlay_[i] = 0xff;
                }
                command_state_ = CommandState::idle;
                break;
        }
    }

    void Flash::reset_command_state() { command_state_ = CommandState::idle; }

    void Flash::clear_overlay() {
        overlay_.clear();
        boot_checkpoint_taken_ = false;
    }

    void Flash::restore_overlay(std::unordered_map<std::uint32_t, std::uint8_t> overlay) {
        overlay_ = std::move(overlay);
        reset_command_state();
        boot_checkpoint_taken_ = false;
    }
} // namespace el9950
