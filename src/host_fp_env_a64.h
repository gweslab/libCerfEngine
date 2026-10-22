#pragma once

#include <cstdint>

#include <cerfengine/force_inline.h>

#include "host_fp_op.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include "host_fp_env_msvc.h"
#elif defined(__clang__)
#include <arm_acle.h>
#endif

namespace cerfengine {

class HostFpEnvA64 {
public:
    /* ARM DDI 0487 M.d C5.2.10 FPSR: OFC bit [2], IXC bit [4]. */
    static constexpr uint32_t kOverflow = 1u << 2;
    static constexpr uint32_t kInexact  = 1u << 4;

    /* ARM DDI 0487 M.d C5.2.10 FPSR: IOC [0], DZC [1], OFC [2], UFC [3],
       IXC [4], IDC [7]; C5.2.8 FPCR "RMode, bits [23:22]", 0b11 Round towards
       Zero (RZ) mode. */
    static constexpr uint64_t kStickyFlags     = 0x9Fu;
    static constexpr uint64_t kRModeMask       = 3ull << 22;
    static constexpr uint64_t kRoundTowardZero = 3ull << 22;

    template <HostFpOp kOp, class T>
    CERFENGINE_FORCEINLINE static T Run(T a, T b, uint32_t* raised) {
#if defined(_MSC_VER) && !defined(__clang__)
        uint32_t  status = 0u;
        const T   r      = HostFpEnvMsvc::Run(kOp, a, b, false, &status);
        *raised          = status & (kOverflow | kInexact);
#else
        uint64_t  status = 0u;
        const T   r      = Fp<kOp, false>(a, b, ReadFpsr() & ~kStickyFlags, 0u, &status);
        *raised          = static_cast<uint32_t>(status) & (kOverflow | kInexact);
#endif
        return r;
    }

    template <HostFpOp kOp, class T>
    CERFENGINE_FORCEINLINE static T RunTowardZero(T a, T b) {
#if defined(_MSC_VER) && !defined(__clang__)
        uint32_t status = 0u;
        return HostFpEnvMsvc::Run(kOp, a, b, true, &status);
#else
        const uint64_t fpcr = ReadFpcr();
        return Fp<kOp, true>(a, b, (fpcr & ~kRModeMask) | kRoundTowardZero, fpcr, nullptr);
#endif
    }

#if !(defined(_MSC_VER) && !defined(__clang__))
private:
#if defined(__clang__)
    CERFENGINE_FORCEINLINE static uint64_t ReadFpcr() { return __arm_rsr64("fpcr"); }
    CERFENGINE_FORCEINLINE static uint64_t ReadFpsr() { return __arm_rsr64("fpsr"); }
#elif defined(__GNUC__)
    CERFENGINE_FORCEINLINE static uint64_t ReadFpcr() { return __builtin_aarch64_get_fpcr64(); }
    CERFENGINE_FORCEINLINE static uint64_t ReadFpsr() { return __builtin_aarch64_get_fpsr64(); }
#else
#error "host_fp_env_a64.h has no FPCR/FPSR access for this compiler"
#endif

#define CERFENGINE_A64_FP_OP(op_text)                                                       \
    do {                                                                                    \
        if constexpr (kTowardZero) {                                                        \
            __asm__ volatile("msr fpcr, %[in]\n\t" op_text "\n\tmsr fpcr, %[back]"          \
                             : [a] "+w"(a)                                                  \
                             : [b] "w"(b), [in] "r"(in), [back] "r"(back));                 \
        } else {                                                                            \
            __asm__ volatile("msr fpsr, %[in]\n\t" op_text "\n\tmrs %[out], fpsr"           \
                             : [a] "+w"(a), [out] "=r"(*out)                                \
                             : [b] "w"(b), [in] "r"(in));                                   \
        }                                                                                   \
    } while (0)

    template <HostFpOp kOp, bool kTowardZero, class T>
    CERFENGINE_FORCEINLINE static T Fp(T a, T b, uint64_t in, [[maybe_unused]] uint64_t back,
                                       [[maybe_unused]] uint64_t* out) {
        static_assert(sizeof(T) == 4u || sizeof(T) == 8u, "single or double precision only");
        constexpr bool kSingle = sizeof(T) == 4u;
        if constexpr (kOp == HostFpOp::kAdd) {
            if constexpr (kSingle) CERFENGINE_A64_FP_OP("fadd %s[a], %s[a], %s[b]");
            else                   CERFENGINE_A64_FP_OP("fadd %d[a], %d[a], %d[b]");
        } else if constexpr (kOp == HostFpOp::kSub) {
            if constexpr (kSingle) CERFENGINE_A64_FP_OP("fsub %s[a], %s[a], %s[b]");
            else                   CERFENGINE_A64_FP_OP("fsub %d[a], %d[a], %d[b]");
        } else if constexpr (kOp == HostFpOp::kMul) {
            if constexpr (kSingle) CERFENGINE_A64_FP_OP("fmul %s[a], %s[a], %s[b]");
            else                   CERFENGINE_A64_FP_OP("fmul %d[a], %d[a], %d[b]");
        } else if constexpr (kOp == HostFpOp::kDiv) {
            if constexpr (kSingle) CERFENGINE_A64_FP_OP("fdiv %s[a], %s[a], %s[b]");
            else                   CERFENGINE_A64_FP_OP("fdiv %d[a], %d[a], %d[b]");
        } else {
            if constexpr (kSingle) CERFENGINE_A64_FP_OP("fsqrt %s[a], %s[a]");
            else                   CERFENGINE_A64_FP_OP("fsqrt %d[a], %d[a]");
        }
        return a;
    }

#undef CERFENGINE_A64_FP_OP
#endif
};

}
