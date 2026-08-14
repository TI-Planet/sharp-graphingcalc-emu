#include "el9950/lcd.hpp"

#include <algorithm>

namespace el9950
{
    void Lcd::reset() {
        vram_.fill(0);
        framebuffer_.fill(0);
        page_ = column_ = start_line_ = 0;
        contrast_ = 0x20;
        display_on_ = inverted_ = adc_reverse_ = com_reverse_ = all_pixels_ = expect_contrast_ = false;
        dirty_ = true;
    }

    void Lcd::command(std::uint8_t value) {
        if (expect_contrast_) {
            contrast_ = value & 0x3f;
            expect_contrast_ = false;
            return;
        }
        if ((value & 0xf8) == 0xb0)
            page_ = value & 0x07;
        else if ((value & 0xf0) == 0x00)
            column_ = (column_ & 0xf0) | (value & 0x0f);
        else if ((value & 0xf0) == 0x10)
            column_ = (column_ & 0x0f) | ((value & 0x0f) << 4);
        else if ((value & 0xc0) == 0x40)
            start_line_ = value & 0x3f;
        else
            switch (value) {
                case 0xae:
                    display_on_ = false;
                    break;
                case 0xaf:
                    display_on_ = true;
                    break;
                // The EL-9950 panel is mounted opposite the controller's raw segment
                // numbering: its normal firmware command A1 maps logical column 0 to the
                // left edge of the photographed display.
                case 0xa0:
                    adc_reverse_ = true;
                    break;
                case 0xa1:
                    adc_reverse_ = false;
                    break;
                case 0xa4:
                    all_pixels_ = false;
                    break;
                case 0xa5:
                    all_pixels_ = true;
                    break;
                case 0xa6:
                    inverted_ = false;
                    break;
                case 0xa7:
                    inverted_ = true;
                    break;
                case 0xc0:
                    com_reverse_ = true;
                    break;
                case 0xc8:
                    com_reverse_ = false;
                    break;
                case 0x81:
                    expect_contrast_ = true;
                    break;
                case 0xe2:
                    reset();
                    break;
                default:
                    break;
            }
        dirty_ = true;
    }

    void Lcd::data(std::uint8_t value) {
        if (column_ < kLcdWidth)
            vram_[page_ * kLcdWidth + column_] = value;
        column_ = static_cast<std::uint8_t>((column_ + 1) % kLcdWidth);
        dirty_ = true;
    }

    std::uint8_t Lcd::read_data() const { return column_ < kLcdWidth ? vram_[page_ * kLcdWidth + column_] : 0; }

    void Lcd::rebuild_framebuffer() const {
        for (std::size_t y = 0; y < kLcdHeight; ++y) {
            const auto source_y = (com_reverse_ ? (63 - y + start_line_) : (y + start_line_)) & 63;
            for (std::size_t x = 0; x < kLcdWidth; ++x) {
                const auto source_x = adc_reverse_ ? kLcdWidth - 1 - x : x;
                bool pixel = all_pixels_ || ((vram_[(source_y / 8) * kLcdWidth + source_x] >> (source_y & 7)) & 1);
                if (!display_on_)
                    pixel = false;
                if (inverted_)
                    pixel = !pixel;
                framebuffer_[y * kLcdWidth + x] = pixel ? 0xff : 0;
            }
        }
        dirty_ = false;
    }

    const std::array<std::uint8_t, kLcdWidth * kLcdHeight>& Lcd::framebuffer() const {
        if (dirty_)
            rebuild_framebuffer();
        return framebuffer_;
    }

    void Lcd::restore_vram(const std::array<std::uint8_t, kLcdWidth * 8>& vram) {
        vram_ = vram;
        dirty_ = true;
    }

    LcdState Lcd::state() const {
        return {
            vram_, page_, column_, start_line_, contrast_, display_on_, inverted_, adc_reverse_, com_reverse_, all_pixels_, expect_contrast_
        };
    }

    void Lcd::set_state(const LcdState& s) {
        vram_ = s.vram;
        page_ = s.page;
        column_ = s.column;
        start_line_ = s.start_line;
        contrast_ = s.contrast;
        display_on_ = s.display_on;
        inverted_ = s.inverted;
        adc_reverse_ = s.adc_reverse;
        com_reverse_ = s.com_reverse;
        all_pixels_ = s.all_pixels;
        expect_contrast_ = s.expect_contrast;
        dirty_ = true;
    }
} // namespace el9950
