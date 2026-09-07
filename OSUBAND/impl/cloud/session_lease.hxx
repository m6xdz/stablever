#pragma once
#include <algorithm>
#include <cstdint>

namespace cloud {
// Monotonic deadline: a network outage may retain a *previously verified*
// session briefly, never extend it past subscription expiry, or undo a denial.
class session_lease {
    uint64_t deadline_ = 0;
public:
    void accept(uint64_t monotonic_ms, int64_t server_ms, int64_t expires_ms = 0) {
        const auto remaining = expires_ms > 0 ? std::max<int64_t>(0, expires_ms - server_ms) : 120000;
        deadline_ = monotonic_ms + static_cast<uint64_t>(std::min<int64_t>(120000, remaining));
    }
    void deny() { deadline_ = 0; }
    uint64_t deadline() const { return deadline_; }
    bool valid(uint64_t monotonic_ms) const { return deadline_ != 0 && monotonic_ms < deadline_; }
};
}
