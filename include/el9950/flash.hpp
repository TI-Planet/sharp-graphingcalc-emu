#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

namespace el9950
{
    class Flash
    {
    public:
        using BootWriteCallback = std::function<void()>;
        void load(std::span<const std::uint8_t> immutable_rom);
        std::uint8_t read(std::uint32_t offset) const;
        void write(std::uint32_t offset, std::uint8_t value);
        void reset_command_state();
        void clear_overlay();

        const std::unordered_map<std::uint32_t, std::uint8_t>&
        overlay() const { return overlay_; }

        void restore_overlay(std::unordered_map<std::uint32_t, std::uint8_t> overlay);

        void
        set_boot_write_callback(BootWriteCallback callback) { boot_write_callback_ = std::move(callback); }

    private:
        enum class CommandState : std::uint8_t { idle, unlock1, unlock2, program, erase1, erase2, erase3 };

        void program(std::uint32_t offset, std::uint8_t value);
        void erase_sector(std::uint32_t offset);
        void before_mutation(std::uint32_t offset);
        std::vector<std::uint8_t> rom_;
        std::unordered_map<std::uint32_t, std::uint8_t> overlay_;
        CommandState command_state_{CommandState::idle};
        BootWriteCallback boot_write_callback_;
        bool boot_checkpoint_taken_{};
    };
} // namespace el9950
