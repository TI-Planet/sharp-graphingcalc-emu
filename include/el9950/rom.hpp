#pragma once

#include "el9950/sha256.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace el9950
{
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

    private:
        std::vector<std::uint8_t> bytes_;
        Sha256Digest digest_{};
        std::string sha_hex_;
    };
} // namespace el9950
