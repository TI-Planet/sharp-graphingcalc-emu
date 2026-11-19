#include "el9950/skin.hpp"
#include <array>

namespace el9950
{
    namespace
    {
        constexpr KeyRect hits9300[]{
            {"calc", 101, 609, 113, 69}, {Key::graph, 248, 609, 110, 69},
            {Key::prgm, 390, 609, 113, 69}, {Key::stat, 535, 609, 109, 69},
            {Key::on, 675, 609, 111, 69},
            {"eqtn", 102, 729, 95, 54}, {"range", 227, 729, 93, 54}, {Key::zoom, 349, 729, 90, 54},
            {Key::math, 102, 814, 95, 53}, {"menu", 228, 814, 93, 53}, {"quit", 351, 814, 89, 53},
            {Key::left, 472, 726, 85, 139}, {Key::up, 558, 726, 139, 62},
            {Key::down, 558, 804, 139, 61}, {Key::right, 699, 727, 85, 138},
            {Key::second, 102, 897, 95, 54}, {Key::alpha, 228, 897, 94, 54}, {"setup", 351, 897, 89, 54},
            {Key::del, 469, 897, 89, 54}, {Key::backspace, 586, 897, 87, 54}, {Key::clear, 700, 897, 88, 54},
            {Key::sin, 102, 984, 95, 51}, {Key::cos, 228, 984, 92, 51}, {Key::tan, 349, 984, 90, 51},
            {Key::square, 468, 984, 90, 51}, {Key::log, 584, 984, 90, 51}, {Key::ln, 701, 984, 88, 51},
            {Key::exp, 102, 1066, 95, 52}, {Key::fraction, 228, 1066, 92, 52}, {Key::power, 349, 1066, 90, 52},
            {"sqrt", 468, 1066, 88, 52}, {Key::sto, 586, 1066, 88, 52}, {Key::variable, 702, 1066, 88, 52},
            {Key::n7, 101, 1152, 113, 64}, {Key::n8, 249, 1152, 111, 64}, {Key::n9, 393, 1152, 110, 64},
            {Key::lparen, 536, 1152, 110, 64}, {Key::rparen, 677, 1152, 110, 64},
            {Key::n4, 101, 1247, 113, 64}, {Key::n5, 249, 1247, 111, 64}, {Key::n6, 393, 1247, 110, 64},
            {Key::multiply, 536, 1247, 110, 64}, {Key::divide, 677, 1247, 110, 64},
            {Key::n1, 101, 1342, 113, 64}, {Key::n2, 249, 1342, 111, 64}, {Key::n3, 393, 1342, 110, 64},
            {Key::plus, 536, 1342, 110, 64}, {Key::minus, 677, 1342, 110, 64},
            {Key::n0, 101, 1442, 113, 65}, {Key::decimal, 249, 1442, 111, 65}, {Key::negative, 393, 1442, 110, 65},
            {Key::enter, 536, 1442, 251, 65},
        };

        constexpr std::array<KeyRect, 54> hits9950{
            {
                {Key::y_equals, 92, 688, 112, 68}, {Key::graph, 227, 688, 111, 68}, {Key::table, 360, 688, 112, 68},
                {Key::window, 494, 688, 114, 68}, {Key::zoom, 627, 688, 114, 68}, {Key::trace, 759, 688, 115, 68},
                {Key::option, 92, 800, 112, 66}, {Key::stat_plot, 227, 800, 112, 66},
                {Key::slide_show, 360, 800, 112, 66},
                {Key::up, 625, 786, 150, 59}, {Key::left, 527, 831, 82, 90}, {Key::right, 792, 831, 82, 90},
                {Key::down, 625, 921, 150, 58},
                {Key::on, 92, 915, 113, 67}, {Key::second, 226, 915, 114, 67}, {Key::alpha, 360, 915, 114, 67},
                {Key::math, 92, 1051, 112, 65}, {Key::stat, 227, 1051, 112, 65}, {Key::prgm, 360, 1051, 112, 65},
                {Key::del, 494, 1051, 114, 65}, {Key::backspace, 627, 1051, 114, 65}, {Key::clear, 759, 1051, 115, 65},
                {Key::sin, 92, 1161, 112, 66}, {Key::cos, 227, 1161, 112, 66}, {Key::tan, 360, 1161, 112, 66},
                {Key::log, 494, 1161, 114, 66}, {Key::ln, 627, 1161, 114, 66}, {Key::square, 759, 1161, 115, 66},
                {Key::fraction, 92, 1270, 112, 66}, {Key::mixed_fraction, 227, 1270, 112, 66},
                {Key::power, 360, 1270, 112, 66},
                {Key::comma, 494, 1270, 114, 66}, {Key::sto, 627, 1270, 114, 66}, {Key::variable, 759, 1270, 115, 66},
                {Key::n7, 88, 1390, 132, 91}, {Key::n8, 253, 1390, 133, 91}, {Key::n9, 417, 1390, 133, 91},
                {Key::lparen, 580, 1390, 133, 91}, {Key::rparen, 743, 1390, 133, 91},
                {Key::n4, 88, 1530, 132, 91}, {Key::n5, 253, 1530, 133, 91}, {Key::n6, 417, 1530, 133, 91},
                {Key::multiply, 580, 1530, 133, 91}, {Key::divide, 743, 1530, 133, 91},
                {Key::n1, 88, 1667, 132, 91}, {Key::n2, 253, 1667, 133, 91}, {Key::n3, 417, 1667, 133, 91},
                {Key::plus, 580, 1667, 133, 91}, {Key::minus, 743, 1667, 133, 91},
                {Key::n0, 88, 1805, 132, 91}, {Key::decimal, 253, 1805, 133, 91}, {Key::negative, 417, 1805, 133, 91},
                {Key::exp, 580, 1805, 133, 91}, {Key::enter, 743, 1805, 133, 91}
            }
        };

        constexpr std::array<KeyRect, 54> hits9900{{
            {Key::y_equals, 91, 540, 78, 67},
            {Key::graph, 200, 531, 79, 70},
            {Key::table, 307, 525, 80, 70},
            {Key::window, 418, 526, 80, 70},
            {Key::zoom, 526, 531, 81, 70},
            {Key::trace, 638, 542, 80, 68},
            {Key::option, 91, 636, 78, 66},
            {Key::stat_plot, 200, 636, 79, 66},
            {Key::slide_show, 307, 636, 80, 66},
            {Key::up, 513, 619, 96, 62},
            {Key::left, 404, 651, 93, 82},
            {Key::right, 620, 650, 96, 83},
            {Key::down, 513, 724, 96, 63},
            {Key::on, 89, 728, 82, 57},
            {Key::second, 199, 726, 81, 57},
            {Key::alpha, 307, 726, 81, 57},
            {Key::math, 103, 850, 76, 48},
            {Key::stat, 207, 850, 76, 48},
            {Key::prgm, 311, 850, 76, 48},
            {Key::del, 415, 850, 76, 48},
            {Key::backspace, 519, 850, 76, 48},
            {Key::clear, 623, 850, 76, 48},
            {Key::sin, 103, 933, 76, 48},
            {Key::cos, 207, 933, 76, 48},
            {Key::tan, 311, 933, 76, 48},
            {Key::log, 415, 933, 76, 48},
            {Key::ln, 519, 933, 76, 48},
            {Key::square, 623, 933, 76, 48},
            {Key::fraction, 103, 1015, 76, 48},
            {Key::mixed_fraction, 207, 1015, 76, 48},
            {Key::power, 311, 1015, 76, 48},
            {Key::comma, 415, 1015, 76, 48},
            {Key::sto, 519, 1015, 76, 48},
            {Key::variable, 623, 1015, 76, 48},
            {Key::n7, 103, 1104, 99, 67},
            {Key::n8, 227, 1104, 99, 67},
            {Key::n9, 352, 1104, 99, 67},
            {Key::lparen, 477, 1104, 99, 67},
            {Key::rparen, 602, 1104, 99, 67},
            {Key::n4, 103, 1204, 99, 67},
            {Key::n5, 227, 1204, 99, 67},
            {Key::n6, 352, 1204, 99, 67},
            {Key::multiply, 477, 1204, 99, 67},
            {Key::divide, 602, 1204, 99, 67},
            {Key::n1, 103, 1305, 99, 67},
            {Key::n2, 227, 1305, 99, 67},
            {Key::n3, 352, 1305, 99, 67},
            {Key::plus, 477, 1305, 99, 67},
            {Key::minus, 602, 1305, 99, 67},
            {Key::n0, 103, 1406, 99, 67},
            {Key::decimal, 227, 1406, 99, 67},
            {Key::negative, 352, 1406, 99, 67},
            {Key::exp, 477, 1406, 99, 67},
            {Key::enter, 602, 1406, 99, 67},
        }};
    }
    Skin skin_for(Model model, KeyboardSide side) {
        if (model == Model::el9300)
            return {"SHARP_EL-9300.webp", 876, 1598, 194, 138, 496, 356, hits9300};
        if (!has_reversible_keyboard(model))
            return {"SHARP_EL-9950G.webp", 961, 2000, 102, 151, 762, 437, hits9950};
        std::string_view image;
        const bool basic = side == KeyboardSide::basic;
        switch (model) {
        case Model::el9900:
            image = basic ? "SHARP_EL-9900_simple-keyboard.webp" : "SHARP_EL-9900_advanced-keyboard.webp"; break;
        case Model::el9900g:
            image = basic ? "SHARP_EL-9900G_simple-keyboard.webp" : "SHARP_EL-9900G_advanced-keyboard.webp"; break;
        default:
            image = basic ? "SHARP_EL-9900G_SII_simple-keyboard.webp" : "SHARP_EL-9900G_SII_advanced-keyboard.webp"; break;
        }
        return {image, 809, 1600, 110, 101, 590, 344, hits9900};
    }

    std::optional<SkinKey> skin_key_at(const Skin& skin, float x, float y) {
        for (const auto& r : skin.keys)
            if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h)
                return r.key;
        return std::nullopt;
    }
}
