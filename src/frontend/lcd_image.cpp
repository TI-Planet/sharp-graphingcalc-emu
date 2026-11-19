#include "lcd_image.hpp"

#include "el9950/state.hpp"

namespace
{
using Flag = sharp::LcdAnnunciator;

// The segment positions follow the markings across the real LCD glass.
// At twice the dot-matrix resolution, lettering can fit in a short row.
constexpr struct {
    Flag flag;
    unsigned x;
    std::string_view rows[7];
} segments[] = {
    {Flag::left, 0, {
        "...#...", "..##...", ".######", "#######", ".######", "..##...", "...#..."}},
    {Flag::busy, 14, {
        "####..#...#..####.#...#", "#...#.#...#.#......#.#.", "#...#.#...#.#......#.#.",
        "####..#...#..###....#..", "#...#.#...#.....#...#..", "#...#.#...#.....#...#..",
        "####...###..####....#.."}},
    {Flag::second, 47, {
        ".###............#..#####", "#...#...........#..#....", "....#.###....####..#....",
        "..##..#..#..#...#..####.", ".#....#..#..#...#..#....", "#.....#..#..#...#..#....",
        "#####.#..#...####..#...."}},
    {Flag::alpha, 77, {
        ".###..#.....####..#...#..###.", "#...#.#.....#...#.#...#.#...#", "#...#.#.....#...#.#...#.#...#",
        "#####.#.....####..#####.#####", "#...#.#.....#.....#...#.#...#", "#...#.#.....#.....#...#.#...#",
        "#...#.#####.#.....#...#.#...#"}},
    {Flag::insert, 111, {
        "###.#...#..####", ".#..##..#.#....", ".#..#.#.#.#....", ".#..#..#...###.",
        ".#..#...#.....#", ".#..#...#.....#", "###.#...#.####."}},
    {Flag::battery, 133, {
        "###################", "##..###.##...#...##", "##.#.#.#.##.###.###",
        "##..##...##.###.###", "##.#.#.#.##.###.###", "##..##.#.##.###.###",
        "###################"}},
    {Flag::up, 161, {
        "...#...", "..###..", ".#####.", "#######", "..###..", "..###..", "..###.."}},
    {Flag::down, 171, {
        "..###..", "..###..", "..###..", "#######", ".#####.", "..###..", "...#..."}},
    {Flag::right, 185, {
        "...#...", "...##..", "######.", "#######", "######.", "...##..", "...#..."}},
};

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (const auto shift : {24, 16, 8, 0})
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void append_chunk(std::vector<std::uint8_t>& png, std::string_view type, std::span<const std::uint8_t> data)
{
    append_u32(png, static_cast<std::uint32_t>(data.size()));
    const auto start = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), data.begin(), data.end());
    append_u32(png, el9950::crc32(std::span(png).subspan(start)));
}
}

LcdImage make_lcd_image(const sharp::MachineInfo& info, std::span<const std::uint8_t> fb, std::uint16_t flags)
{
    if (info.id != "el92xx")
        return {info.lcd_width, info.lcd_height, {fb.begin(), fb.end()}};

    constexpr unsigned header = 10;
    constexpr auto scale = LcdImage::sc62015_texels_per_dot;
    LcdImage image{scale * info.lcd_width, scale * info.lcd_height + header, {}};
    image.dots.resize(image.width * image.height);
    for (const auto& segment : segments) {
        if (!(flags & static_cast<std::uint16_t>(segment.flag)))
            continue;
        for (unsigned y = 0; y < std::size(segment.rows); ++y)
            for (unsigned x = 0; x < segment.rows[y].size(); ++x)
                image.dots[(y + 1) * image.width + segment.x + x] = segment.rows[y][x] == '#';
    }
    for (unsigned y = header; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x)
            image.dots[y * image.width + x] = fb[((y - header) / scale) * info.lcd_width + x / scale];
    return image;
}

std::vector<std::uint8_t> encode_lcd_png(const LcdImage& image)
{
    // Our two LCD colors fit in a 1-bit indexed PNG. Even the larger image
    // fits in one uncompressed DEFLATE block, so no codec dependency is needed.
    const auto stride = 1 + (image.width + 7) / 8;
    std::vector<std::uint8_t> rows(stride * image.height); // Filter 0 at the start of each row.
    for (unsigned y = 0; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x)
            if (image.dots[y * image.width + x])
                rows[y * stride + 1 + x / 8] |= 0x80U >> (x % 8);

    if (rows.size() > 0xffff)
        throw std::runtime_error("LCD screenshot is too large");
    const auto size = static_cast<std::uint16_t>(rows.size());
    const auto inverted_size = static_cast<std::uint16_t>(~size);
    std::vector<std::uint8_t> zlib{0x78, 0x01, 0x01,
        std::uint8_t(size), std::uint8_t(size >> 8), std::uint8_t(inverted_size), std::uint8_t(inverted_size >> 8)};
    zlib.insert(zlib.end(), rows.begin(), rows.end());
    std::uint32_t a = 1, b = 0;
    for (const auto byte : rows) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    append_u32(zlib, (b << 16) | a);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<std::uint8_t> header;
    append_u32(header, image.width);
    append_u32(header, image.height);
    header.insert(header.end(), {1, 3, 0, 0, 0}); // 1 bit, indexed color, no interlacing.
    constexpr auto bg = LcdImage::background;
    constexpr auto fg = LcdImage::foreground;
    const std::uint8_t palette[]{std::uint8_t(bg >> 24), std::uint8_t(bg >> 16), std::uint8_t(bg >> 8),
                                 std::uint8_t(fg >> 24), std::uint8_t(fg >> 16), std::uint8_t(fg >> 8)};
    append_chunk(png, "IHDR", header);
    append_chunk(png, "PLTE", palette);
    append_chunk(png, "IDAT", zlib);
    append_chunk(png, "IEND", {});
    return png;
}
