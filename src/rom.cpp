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
        if (size != static_cast<std::streamoff>(kFlashSize) && size != 0x40000)
            throw std::runtime_error("raw Sharp ROM must be 0x100000 (EL-99xx) or 0x40000 (EL-9200/9300) bytes");
        input.seekg(0);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("failed to read complete ROM: " + path.string());
        return from_bytes(std::move(bytes));
    }

    RomImage RomImage::from_bytes(std::vector<std::uint8_t> bytes) {
        if (bytes.size() != kFlashSize && bytes.size() != 0x40000)
            throw std::runtime_error("raw Sharp ROM must be 0x100000 (EL-99xx) or 0x40000 (EL-9200/9300) bytes");
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
        case Model::el9200: return "EL-9200";
        case Model::el9300: return "EL-9300";
        default: return "Unknown Sharp EL-99xx";
        }
    }

    bool has_reversible_keyboard(Model model) {
        return model == Model::el9900 || model == Model::el9900g || model == Model::el9900g_sii;
    }

    void RomImage::identify() {
        if (family() == HardwareFamily::el92xx) {
            const std::string_view data(reinterpret_cast<const char*>(bytes_.data()), bytes_.size());
            for (const auto model : {Model::el9200, Model::el9300}) {
                const auto banner = model == Model::el9200 ? "EL 9200" : "EL 9300";
                const auto position = data.find(banner);
                if (position == std::string_view::npos) continue;
                const auto diagnostic = data.substr(position, 48);
                const auto ver = diagnostic.find("Ver.");
                if (ver == std::string_view::npos) continue;
                const auto digit = diagnostic.find_first_of("0123456789", ver + 4);
                if (digit == std::string_view::npos || digit + 4 > diagnostic.size()) continue;
                const auto version = diagnostic.substr(digit, 4);
                if (version[1] != '.' || version[2] < '0' || version[2] > '9' || version[3] < '0' || version[3] > '9') continue;
                model_ = model;
                version_ = version;
                return;
            }
            return;
        }
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
