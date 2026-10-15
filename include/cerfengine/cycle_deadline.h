#pragma once

#include <cstdint>

namespace cerfengine {

inline constexpr uint64_t kMaxCycleDeadlineAhead = 1ull << 30;

inline bool CycleDeadlineReached(uint32_t counter, uint32_t deadline) {
    return static_cast<int32_t>(counter - deadline) >= 0;
}

inline uint32_t CycleDeadline(uint32_t counter, uint64_t cycles_ahead) {
    return counter + static_cast<uint32_t>(cycles_ahead < kMaxCycleDeadlineAhead
                                               ? cycles_ahead
                                               : kMaxCycleDeadlineAhead);
}

}
