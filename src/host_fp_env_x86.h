#pragma once

#include <cstdint>
#include <xmmintrin.h>

#include <cerfengine/force_inline.h>

namespace cerfengine {

class HostFpEnvX86 {
public:
    static constexpr uint32_t kOverflow = 1u << 3;
    static constexpr uint32_t kInexact  = 1u << 5;

    struct Saved {
        uint32_t csr;
    };

    CERFENGINE_FORCEINLINE static Saved BeginClean() {
        const uint32_t csr = _mm_getcsr();
        _mm_setcsr(csr & ~kSticky);
        return Saved{csr};
    }

    CERFENGINE_FORCEINLINE static uint32_t Raised() {
        return _mm_getcsr() & (kOverflow | kInexact);
    }

    CERFENGINE_FORCEINLINE static void RoundTowardZero(const Saved& saved) {
        _mm_setcsr((saved.csr & ~kSticky) | kRoundTowardZero);
    }

    CERFENGINE_FORCEINLINE static void Restore(const Saved& saved) {
        _mm_setcsr(saved.csr & ~kSticky);
    }

private:
    static constexpr uint32_t kSticky = 0x3Fu;

    /* Intel SDM Vol. 1 section 10.2.3.2 (p. 10-4): "Bits 13 and 14 of the
       MXCSR register (the rounding control [RC] field) control how the results
       of SIMD floating-point instructions are rounded". */
    static constexpr uint32_t kRoundTowardZero = _MM_ROUND_TOWARD_ZERO;
};

}
