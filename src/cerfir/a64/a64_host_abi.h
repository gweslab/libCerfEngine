#pragma once

#include <cstdint>

#include "../../a64_emit.h"

namespace cerfengine::cerfir {

/* AAPCS64 2025Q4 Table 2: r0-r7 parameter/result registers, r19-r28 callee-saved;
   "The Stack": SP mod 16 = 0 at every access through SP. */
inline constexpr uint8_t  kA64ArgRegs[8]  = {a64::kX0, a64::kX1, a64::kX2, a64::kX3,
                                             a64::kX4, a64::kX5, a64::kX6, a64::kX7};
inline constexpr uint32_t kA64ArgRegCount = 8;

inline constexpr int32_t  kA64FrameBytes    = 240;
inline constexpr int32_t  kA64OutArgBytes   = 96;
inline constexpr int32_t  kA64ReturnSlot    = 96;
inline constexpr int32_t  kA64SpillBase     = 112;
inline constexpr uint32_t kA64SpillSlotSize = 4;

#if defined(__APPLE__)
inline constexpr bool kA64PackedStackArgs = true;
#else
inline constexpr bool kA64PackedStackArgs = false;
#endif

class A64StackArgs {
public:
    CERFENGINE_FORCEINLINE int32_t Place(uint32_t bytes) {
        const uint32_t size = kA64PackedStackArgs ? bytes : 8u;
        next_               = (next_ + size - 1u) & ~(size - 1u);
        const int32_t at    = static_cast<int32_t>(next_);
        next_ += size;
        return at;
    }

private:
    uint32_t next_ = 0;
};

}
