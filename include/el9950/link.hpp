#pragma once

#include <array>
#include <cstdint>

namespace el9950
{
    class LinkPort
    {
    public:
        void drive(std::uint8_t line, bool released);
        bool sample(std::uint8_t line) const;

        std::array<bool, 3>
        drive_state() const { return drive_released_; }

        void
        restore(std::array<bool, 3> state) { drive_released_ = state; }

        void
        connect(LinkPort* peer) { peer_ = peer; }

    private:
        std::array<bool, 3> drive_released_{true, true, true};
        LinkPort* peer_{};
    };

    void connect(LinkPort& a, LinkPort& b);
} // namespace el9950
