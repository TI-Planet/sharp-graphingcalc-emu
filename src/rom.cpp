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
            throw std::runtime_error("raw EL-9950 ROM must be exactly 0x100000 bytes");
        input.seekg(0);
        std::vector<std::uint8_t> bytes(kFlashSize);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("failed to read complete ROM: " + path.string());
        return from_bytes(std::move(bytes));
    }

    RomImage RomImage::from_bytes(std::vector<std::uint8_t> bytes) {
        if (bytes.size() != kFlashSize)
            throw std::runtime_error("raw EL-9950 ROM must be exactly 0x100000 bytes");
        RomImage result;
        result.bytes_ = std::move(bytes);
        result.digest_ = sha256(result.bytes_);
        result.sha_hex_ = hex_sha256(result.digest_);
        return result;
    }

    bool RomImage::is_known_120() const { return sha_hex_ == kKnownRom120Sha256; }
} // namespace el9950
