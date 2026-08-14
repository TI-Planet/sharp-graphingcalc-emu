#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace el9950
{
    enum class Key : std::uint8_t
    {
        y_equals, graph, table, window, zoom, trace,
        option, stat_plot, slide_show, up, left, right, down,
        on, second, alpha, math, stat, prgm, del, backspace, clear,
        sin, cos, tan, log, ln, square, fraction, mixed_fraction, power,
        comma, sto, variable, n7, n8, n9, lparen, rparen, n4, n5, n6,
        multiply, divide, n1, n2, n3, plus, minus, n0, decimal, negative,
        exp, enter, count
    };

    struct KeyCoordinate
    {
        std::uint8_t output;
        std::uint8_t input;
        std::uint8_t raw_code;
    };

    class Keyboard
    {
    public:
        void reset();
        void set(Key key, bool pressed);
        bool pressed(Key key) const;
        std::uint8_t read_inputs(std::uint8_t active_outputs) const;

        std::uint64_t
        pressed_mask() const { return pressed_; }

        void
        restore_mask(std::uint64_t mask) { pressed_ = mask; }

        bool
        on_pressed() const { return pressed(Key::on); }

        static KeyCoordinate coordinate(Key key);
        static std::string_view id(Key key);
        static std::string_view name(Key key);
        static std::optional<Key> from_id(std::string_view id);

    private:
        std::uint64_t pressed_{};
    };
} // namespace el9950
