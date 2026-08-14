#include "el9950/link.hpp"

namespace el9950
{
    void LinkPort::drive(std::uint8_t line, bool released) {
        if (line < 3)
            drive_released_[line] = released;
    }

    bool LinkPort::sample(std::uint8_t line) const {
        if (line >= 3)
            return true;
        return drive_released_[line] && (!peer_ || peer_->drive_released_[line]);
    }

    void connect(LinkPort& a, LinkPort& b) {
        a.connect(&b);
        b.connect(&a);
    }
} // namespace el9950
