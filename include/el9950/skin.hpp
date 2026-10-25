#pragma once

#include "el9950/keyboard.hpp"
#include "el9950/rom.hpp"

#include <span>
#include <string_view>

namespace el9950
{
    struct KeyRect { Key key; float x, y, w, h; };
    struct Skin {
        std::string_view image;
        float width, height;
        float lcd_x, lcd_y, lcd_w, lcd_h;
        std::span<const KeyRect> keys;
    };
    Skin skin_for(Model model, KeyboardSide side);
    std::optional<Key> skin_key_at(const Skin& skin, float x, float y);
}
