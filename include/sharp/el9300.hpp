#pragma once
#include "el9950/keyboard.hpp"
#include <array>
#include <optional>
#include <utility>

namespace sharp
{
struct El9300ExtraKey
{
    std::string_view id;
    unsigned output, input;
};
inline constexpr std::array<El9300ExtraKey, 7> el9300_extra_keys{{
    {"calc", 9, 6}, {"setup", 6, 5}, {"menu", 7, 6}, {"quit", 7, 5},
    {"edit", 8, 7}, {"range", 8, 6}, {"zoom", 8, 5}
}};
// Electrical positions verified from the 1.11 translation table at F8378.
// Other model-specific functions remain accessible by raw matrix position.
inline std::optional<std::pair<unsigned, unsigned>> el9300_key(el9950::Key key)
{
    using K = el9950::Key;
    switch (key)
    {
    case K::enter:
        return {{0, 3}};
    case K::negative:
        return {{0, 4}};
    case K::decimal:
        return {{0, 5}};
    case K::n0:
        return {{0, 6}};
    case K::minus:
        return {{1, 2}};
    case K::plus:
        return {{1, 3}};
    case K::n3:
        return {{1, 4}};
    case K::n2:
        return {{1, 5}};
    case K::n1:
        return {{1, 6}};
    case K::divide:
        return {{2, 2}};
    case K::multiply:
        return {{2, 3}};
    case K::n6:
        return {{2, 4}};
    case K::n5:
        return {{2, 5}};
    case K::n4:
        return {{2, 6}};
    case K::rparen:
        return {{3, 2}};
    case K::lparen:
        return {{3, 3}};
    case K::n9:
        return {{3, 4}};
    case K::n8:
        return {{3, 5}};
    case K::n7:
        return {{3, 6}};
    case K::clear:
        return {{6, 2}};
    case K::second:
        return {{6, 7}};
    case K::alpha:
        return {{6, 6}};
    case K::del:
        return {{6, 4}};
    case K::backspace:
        return {{6, 3}};
    case K::sin:
        return {{5, 7}};
    case K::cos:
        return {{5, 6}};
    case K::tan:
        return {{5, 5}};
    case K::square:
        return {{5, 4}};
    case K::log:
        return {{5, 3}};
    case K::ln:
        return {{5, 2}};
    case K::sto:
        return {{4, 3}};
    case K::variable:
        return {{4, 2}};
    case K::left:
        return {{7, 2}};
    case K::down:
        return {{7, 3}};
    case K::right:
        return {{7, 4}};
    case K::up:
        return {{8, 3}};
    case K::math:
        return {{7, 7}};
    case K::graph:
        return {{9, 5}};
    case K::zoom:
        return {{8, 5}};
    case K::prgm:
        return {{9, 4}};
    case K::stat:
        return {{9, 3}};
    default:
        return std::nullopt;
    }
}
} // namespace sharp
