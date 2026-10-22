#pragma once

#include <cstdint>

#include <cerfengine/force_inline.h>

#include "host_fp_op.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include "host_fp_env_msvc.h"
#else
#include <xmmintrin.h>
#endif

namespace cerfengine {

class HostFpEnvX86 {
public:
    static constexpr uint32_t kOverflow    = 1u << 3;
    static constexpr uint32_t kInexact     = 1u << 5;
    static constexpr uint32_t kStickyFlags = 0x3Fu;

    /* Intel SDM Vol. 1 section 4.8.4.1 (p. 4-19): the MXCSR RC field is bits 13 and 14;
       Table 4-9: RC 11B is "Round toward zero (Truncate)". */
    static constexpr uint32_t kRoundTowardZero = 3u << 13;

    template <HostFpOp kOp, class T>
    CERFENGINE_FORCEINLINE static T Run(T a, T b, uint32_t* raised) {
        uint32_t status = 0u;
#if defined(_MSC_VER) && !defined(__clang__)
        const T r = HostFpEnvMsvc::Run(kOp, a, b, false, &status);
#else
        const T r = Sse<kOp, false>(a, b, _mm_getcsr() & ~kStickyFlags, 0u, &status);
#endif
        *raised = status & (kOverflow | kInexact);
        return r;
    }

    template <HostFpOp kOp, class T>
    CERFENGINE_FORCEINLINE static T RunTowardZero(T a, T b) {
#if defined(_MSC_VER) && !defined(__clang__)
        uint32_t status = 0u;
        return HostFpEnvMsvc::Run(kOp, a, b, true, &status);
#else
        const uint32_t clean = _mm_getcsr() & ~kStickyFlags;
        return Sse<kOp, true>(a, b, clean | kRoundTowardZero, clean, nullptr);
#endif
    }

#if !(defined(_MSC_VER) && !defined(__clang__))
private:
#define CERFENGINE_SSE_OP(op_text)                                                          \
    do {                                                                                    \
        if constexpr (kTowardZero) {                                                        \
            __asm__ volatile("ldmxcsr %[in]\n\t" op_text "\n\tldmxcsr %[back]"              \
                             : [a] "+x"(a)                                                  \
                             : [b] "x"(b), [in] "m"(in), [back] "m"(back));                 \
        } else {                                                                            \
            __asm__ volatile("ldmxcsr %[in]\n\t" op_text "\n\tstmxcsr %[out]"               \
                             : [a] "+x"(a), [out] "=m"(*out)                                \
                             : [b] "x"(b), [in] "m"(in));                                   \
        }                                                                                   \
    } while (0)

    template <HostFpOp kOp, bool kTowardZero, class T>
    CERFENGINE_FORCEINLINE static T Sse(T a, T b, uint32_t in, [[maybe_unused]] uint32_t back,
                                        [[maybe_unused]] uint32_t* out) {
        static_assert(sizeof(T) == 4u || sizeof(T) == 8u, "single or double precision only");
        constexpr bool kSingle = sizeof(T) == 4u;
        if constexpr (kOp == HostFpOp::kAdd) {
            if constexpr (kSingle) CERFENGINE_SSE_OP("addss %[b], %[a]");
            else                   CERFENGINE_SSE_OP("addsd %[b], %[a]");
        } else if constexpr (kOp == HostFpOp::kSub) {
            if constexpr (kSingle) CERFENGINE_SSE_OP("subss %[b], %[a]");
            else                   CERFENGINE_SSE_OP("subsd %[b], %[a]");
        } else if constexpr (kOp == HostFpOp::kMul) {
            if constexpr (kSingle) CERFENGINE_SSE_OP("mulss %[b], %[a]");
            else                   CERFENGINE_SSE_OP("mulsd %[b], %[a]");
        } else if constexpr (kOp == HostFpOp::kDiv) {
            if constexpr (kSingle) CERFENGINE_SSE_OP("divss %[b], %[a]");
            else                   CERFENGINE_SSE_OP("divsd %[b], %[a]");
        } else {
            if constexpr (kSingle) CERFENGINE_SSE_OP("sqrtss %[a], %[a]");
            else                   CERFENGINE_SSE_OP("sqrtsd %[a], %[a]");
        }
        return a;
    }

#undef CERFENGINE_SSE_OP
#endif
};

}
