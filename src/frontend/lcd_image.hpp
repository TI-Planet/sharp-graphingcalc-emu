#pragma once

#include "sharp/machine.hpp"

#include <cstdint>
#include <vector>

struct LcdImage
{
    unsigned width, height;
    std::vector<std::uint8_t> dots;

    static constexpr std::uint32_t background = 0xb6c5b8ff;
    static constexpr std::uint32_t foreground = 0x17201bff;
    static constexpr unsigned sc62015_texels_per_dot = 2;
};

LcdImage make_lcd_image(const sharp::MachineInfo& info, std::span<const std::uint8_t> framebuffer,
                        std::uint16_t flags);
std::vector<std::uint8_t> encode_lcd_png(const LcdImage& image);
