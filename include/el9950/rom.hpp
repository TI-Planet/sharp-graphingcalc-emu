#pragma once

#include "el9950/sha256.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace el9950
{
    enum class Model : std::uint8_t { unknown, el9900, el9900g, el9900g_sii, el9950, el9200, el9300 };
    enum class HardwareFamily : std::uint8_t { el99xx, el92xx };
    std::string_view model_name(Model model);
    bool has_reversible_keyboard(Model model);

    class RomImage
    {
    public:
        static RomImage load(const std::filesystem::path& path);
        static RomImage from_bytes(std::vector<std::uint8_t> bytes);

        std::span<const std::uint8_t>
        bytes() const { return bytes_; }

        const Sha256Digest&
        digest() const { return digest_; }

        const std::string&
        sha256_hex() const { return sha_hex_; }

        bool is_known_120() const;
        Model model() const { return model_; }
        HardwareFamily family() const { return bytes_.size() == 0x40000 ? HardwareFamily::el92xx : HardwareFamily::el99xx; }
        const std::string& version() const { return version_; }

    private:
        void identify();
        Model model_{Model::unknown};
        std::string version_;
        std::vector<std::uint8_t> bytes_;
        Sha256Digest digest_{};
        std::string sha_hex_;
    };
} // namespace el9950
