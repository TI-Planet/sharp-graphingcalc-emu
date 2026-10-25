#include "el9950/rom.hpp"
#include "el9950/constants.hpp"

#include <fstream>
#include <stdexcept>

namespace el9950
{
    RomImage RomImage::load(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
            throw std::runtime_error("cannot open ROM: " + path.string());
        const auto size = input.tellg();
        if (size != static_cast<std::streamoff>(kFlashSize))
            throw std::runtime_error("raw Sharp EL-99xx ROM must be exactly 0x100000 bytes");
        input.seekg(0);
        std::vector<std::uint8_t> bytes(kFlashSize);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("failed to read complete ROM: " + path.string());
        return from_bytes(std::move(bytes));
    }

    RomImage RomImage::from_bytes(std::vector<std::uint8_t> bytes) {
        if (bytes.size() != kFlashSize)
            throw std::runtime_error("raw Sharp EL-99xx ROM must be exactly 0x100000 bytes");
        RomImage result;
        result.bytes_ = std::move(bytes);
        result.digest_ = sha256(result.bytes_);
        result.sha_hex_ = hex_sha256(result.digest_);
        result.identify();
        return result;
    }

    std::string_view model_name(Model model) {
        switch (model) {
        case Model::el9900: return "EL-9900";
        case Model::el9900g: return "EL-9900G";
        case Model::el9900g_sii: return "EL-9900G SII";
        case Model::el9950: return "EL-9950";
        default: return "Unknown Sharp EL-99xx";
        }
    }

    bool has_reversible_keyboard(Model model) {
        return model == Model::el9900 || model == Model::el9900g || model == Model::el9900g_sii;
    }

    void RomImage::identify() {
        // The diagnostic routine at F35810 loads its model banner into XHL,
        // then prints the major byte and packed-BCD minor byte at FFBFE4/E5.
        // Generic EL-9900/9950 strings elsewhere are shared between models.
        if (bytes_[0x35813] != 0x43) return; // ld XHL, imm32
        std::uint32_t address = 0;
        for (unsigned i = 0; i < 4; ++i)
            address |= std::uint32_t(bytes_[0x35814 + i]) << (8 * i);
        if (address < kFlashBase || address > kFlashBase + kFlashSize - 22) return;
        const std::string_view banner(reinterpret_cast<const char*>(bytes_.data() + address - kFlashBase), 22);
        const auto major = bytes_[0xfbfe4], minor = bytes_[0xfbfe5];
        if (major > 9 || (minor >> 4) > 9 || (minor & 15) > 9) return;
        if (!banner.starts_with("EL\x8a") || banner.find("Ver  !") == std::string_view::npos) return;
        // 4.11 retains a 9950 banner, but is EL-9900G SII firmware.
        if (major == 4 && (banner.starts_with("EL\x8a" "9900G SII") || banner.starts_with("EL\x8a" "9950")))
            model_ = Model::el9900g_sii;
        else if (major == 3 && banner.starts_with("EL\x8a" "9900G ")) model_ = Model::el9900g;
        else if (major == 1 && banner.starts_with("EL\x8a" "9950")) model_ = Model::el9950;
        else if (major == 1 && banner.starts_with("EL\x8a" "9900 ")) model_ = Model::el9900;
        if (model_ != Model::unknown)
            version_ = std::to_string(major) + "." + char('0' + (minor >> 4)) + char('0' + (minor & 15));
    }

    bool RomImage::is_known_120() const { return sha_hex_ == kKnownRom120Sha256; }
} // namespace el9950
