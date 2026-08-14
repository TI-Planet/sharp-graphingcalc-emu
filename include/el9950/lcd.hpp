#pragma once

#include "el9950/constants.hpp"

#include <array>
#include <cstdint>

namespace el9950
{
    struct LcdState
    {
        std::array<std::uint8_t, kLcdWidth * 8> vram{};
        std::uint8_t page{}, column{}, start_line{}, contrast{};
        bool display_on{}, inverted{}, adc_reverse{}, com_reverse{}, all_pixels{}, expect_contrast{};
    };

    class Lcd
    {
    public:
        void reset();
        void command(std::uint8_t value);
        void data(std::uint8_t value);
        std::uint8_t read_data() const;
        const std::array<std::uint8_t, kLcdWidth * kLcdHeight>& framebuffer() const;

        const std::array<std::uint8_t, kLcdWidth * 8>&
        vram() const { return vram_; }

        void restore_vram(const std::array<std::uint8_t, kLcdWidth * 8>& vram);

        bool
        display_on() const { return display_on_; }

        bool
        inverted() const { return inverted_; }

        bool
        adc_reverse() const { return adc_reverse_; }

        bool
        com_reverse() const { return com_reverse_; }

        std::uint8_t
        contrast() const { return contrast_; }

        std::uint8_t
        page() const { return page_; }

        std::uint8_t
        column() const { return column_; }

        std::uint8_t
        start_line() const { return start_line_; }

        LcdState state() const;
        void set_state(const LcdState& state);

    private:
        void rebuild_framebuffer() const;
        std::array<std::uint8_t, kLcdWidth * 8> vram_{};
        mutable std::array<std::uint8_t, kLcdWidth * kLcdHeight> framebuffer_{};
        mutable bool dirty_{true};
        std::uint8_t page_{};
        std::uint8_t column_{};
        std::uint8_t start_line_{};
        std::uint8_t contrast_{0x20};
        bool display_on_{};
        bool inverted_{};
        bool adc_reverse_{};
        bool com_reverse_{};
        bool all_pixels_{};
        bool expect_contrast_{};
    };
} // namespace el9950
