#include "el9950/keyboard.hpp"

#include <array>
#include <stdexcept>

namespace el9950
{
    namespace
    {
        constexpr std::array<std::string_view, static_cast<std::size_t>(Key::count)> ids{
            "y_equals", "graph", "table", "window", "zoom", "trace", "option", "stat_plot", "slide_show", "up", "left", "right", "down",
            "on", "second", "alpha", "math", "stat", "prgm", "del", "backspace", "clear", "sin", "cos", "tan", "log", "ln", "square",
            "fraction", "mixed_fraction", "power", "comma", "sto", "variable", "n7", "n8", "n9", "lparen", "rparen", "n4", "n5", "n6",
            "multiply", "divide", "n1", "n2", "n3", "plus", "minus", "n0", "decimal", "negative", "exp", "enter"
        };
        constexpr std::array<std::string_view, static_cast<std::size_t>(Key::count)> names{
            "Y=", "GRAPH", "TABLE", "WINDOW", "ZOOM", "TRACE", "OPTION", "STAT PLOT", "SLIDE SHOW", "UP", "LEFT", "RIGHT", "DOWN",
            "ON", "2ndF", "ALPHA", "MATH", "STAT", "PRGM", "DEL", "BS", "CL", "sin", "cos", "tan", "log", "ln", "x2", "a/b/c", "a b/c",
            "a^b", ",", "STO", "X/theta/T/n", "7", "8", "9", "(", ")", "4", "5", "6", "multiply", "divide", "1", "2", "3", "plus", "minus",
            "0", "decimal", "negative", "Exp", "ENTER"
        };

        // The keyboard scanner returns 0x17 + output + 0x20 * input.  The firmware
        // turns that raw electrical code into an index in the 8x8 translation tables
        // at F043D4.  Keeping the raw code derived from the coordinates prevents the
        // metadata from silently drifting away from the actual matrix wiring.
        constexpr KeyCoordinate matrix(std::uint8_t output, std::uint8_t input) {
            return {output, input, static_cast<std::uint8_t>(0x17U + output + 0x20U * input)};
        }

        constexpr std::array<KeyCoordinate, static_cast<std::size_t>(Key::count)> coordinates{
            {
                matrix(0, 5), matrix(0, 4), matrix(0, 3), matrix(0, 2), matrix(0, 1), matrix(0, 0),
                matrix(0, 6), matrix(0, 7), matrix(1, 4), matrix(1, 1), matrix(1, 3), matrix(1, 0), matrix(1, 2),
                {7, 7, 0x10}, matrix(1, 6), matrix(1, 5), matrix(2, 6), matrix(2, 5), matrix(2, 4), matrix(2, 3), matrix(2, 2),
                matrix(2, 1),
                matrix(2, 7), matrix(3, 5), matrix(3, 4), matrix(3, 3), matrix(3, 2), matrix(2, 0), matrix(3, 7), matrix(3, 6),
                matrix(4, 4), matrix(4, 2), matrix(3, 1), matrix(3, 0), matrix(4, 6), matrix(4, 5), matrix(4, 3), matrix(4, 1),
                matrix(4, 0), matrix(5, 7), matrix(5, 6), matrix(5, 4), matrix(5, 2), matrix(5, 0), matrix(6, 6), matrix(5, 5),
                matrix(5, 3), matrix(5, 1), matrix(6, 0), matrix(6, 5), matrix(6, 4), matrix(6, 3), matrix(6, 2), matrix(6, 1)
            }
        };
    }

    void Keyboard::reset() { pressed_ = 0; }

    void Keyboard::set(Key key, bool is_pressed) {
        const auto bit = std::uint64_t{1} << static_cast<std::uint8_t>(key);
        if (is_pressed)
            pressed_ |= bit;
        else
            pressed_ &= ~bit;
    }

    bool Keyboard::pressed(Key key) const { return (pressed_ >> static_cast<std::uint8_t>(key)) & 1U; }

    std::uint8_t Keyboard::read_inputs(std::uint8_t active_outputs) const {
        std::uint8_t result = 0xff;
        for (std::size_t i = 0; i < coordinates.size(); ++i) {
            if (!(pressed_ & (std::uint64_t{1} << i)))
                continue;
            const auto c = coordinates[i];
            if (c.output < 7 && !(active_outputs & (1U << c.output)))
                result &= static_cast<std::uint8_t>(~(1U << c.input));
        }
        return result;
    }

    KeyCoordinate Keyboard::coordinate(Key key) { return coordinates.at(static_cast<std::size_t>(key)); }
    std::string_view Keyboard::id(Key key) { return ids.at(static_cast<std::size_t>(key)); }
    std::string_view Keyboard::name(Key key, KeyboardSide side) {
        if (side == KeyboardSide::basic) {
            switch (key) {
            case Key::sin: return "Simp";
            case Key::cos: return "to mixed fraction";
            case Key::tan: return "to improper fraction";
            case Key::log: return "to decimal";
            case Key::ln: return "integer divide";
            case Key::variable: return "x";
            default: break;
            }
        }
        return names.at(static_cast<std::size_t>(key));
    }

    std::optional<Key> Keyboard::from_id(std::string_view id) {
        for (std::size_t index = 0; index < ids.size(); ++index)
            if (ids[index] == id)
                return static_cast<Key>(index);
        return std::nullopt;
    }
} // namespace el9950
