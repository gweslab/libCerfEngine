#include "host_fp_env_msvc.h"

#if defined(_MSC_VER) && !defined(__clang__)

#include <intrin.h>

#if defined(_M_ARM64)
#include <arm64_neon.h>

#include "host_fp_env_a64.h"
#else
#include <emmintrin.h>

#include "host_fp_env_x86.h"
#endif

#pragma fenv_access(on)

namespace cerfengine {

namespace {

#if defined(_M_ARM64)
float Compute(HostFpOp op, float a, float b) {
    if (op == HostFpOp::kAdd) return a + b;
    if (op == HostFpOp::kSub) return a - b;
    if (op == HostFpOp::kMul) return a * b;
    if (op == HostFpOp::kDiv) return a / b;
    return vget_lane_f32(vsqrt_f32(vdup_n_f32(a)), 0);
}

double Compute(HostFpOp op, double a, double b) {
    if (op == HostFpOp::kAdd) return a + b;
    if (op == HostFpOp::kSub) return a - b;
    if (op == HostFpOp::kMul) return a * b;
    if (op == HostFpOp::kDiv) return a / b;
    return vget_lane_f64(vsqrt_f64(vdup_n_f64(a)), 0);
}

template <class T>
T RunWithFlags(HostFpOp op, T a, T b, bool toward_zero, uint32_t* status) {
    const uint64_t fpcr = static_cast<uint64_t>(_ReadStatusReg(ARM64_FPCR));
    if (toward_zero) {
        _WriteStatusReg(ARM64_FPCR, static_cast<__int64>((fpcr & ~HostFpEnvA64::kRModeMask) |
                                                         HostFpEnvA64::kRoundTowardZero));
    }
    const uint64_t fpsr = static_cast<uint64_t>(_ReadStatusReg(ARM64_FPSR));
    _WriteStatusReg(ARM64_FPSR, static_cast<__int64>(fpsr & ~HostFpEnvA64::kStickyFlags));
    const T r = Compute(op, a, b);
    *status   = static_cast<uint32_t>(_ReadStatusReg(ARM64_FPSR));
    if (toward_zero) _WriteStatusReg(ARM64_FPCR, static_cast<__int64>(fpcr));
    return r;
}
#else
float Compute(HostFpOp op, float a, float b) {
    const __m128 x = _mm_set_ss(a);
    const __m128 y = _mm_set_ss(b);
    if (op == HostFpOp::kAdd) return _mm_cvtss_f32(_mm_add_ss(x, y));
    if (op == HostFpOp::kSub) return _mm_cvtss_f32(_mm_sub_ss(x, y));
    if (op == HostFpOp::kMul) return _mm_cvtss_f32(_mm_mul_ss(x, y));
    if (op == HostFpOp::kDiv) return _mm_cvtss_f32(_mm_div_ss(x, y));
    return _mm_cvtss_f32(_mm_sqrt_ss(x));
}

double Compute(HostFpOp op, double a, double b) {
    const __m128d x = _mm_set_sd(a);
    const __m128d y = _mm_set_sd(b);
    if (op == HostFpOp::kAdd) return _mm_cvtsd_f64(_mm_add_sd(x, y));
    if (op == HostFpOp::kSub) return _mm_cvtsd_f64(_mm_sub_sd(x, y));
    if (op == HostFpOp::kMul) return _mm_cvtsd_f64(_mm_mul_sd(x, y));
    if (op == HostFpOp::kDiv) return _mm_cvtsd_f64(_mm_div_sd(x, y));
    return _mm_cvtsd_f64(_mm_sqrt_sd(x, x));
}

template <class T>
T RunWithFlags(HostFpOp op, T a, T b, bool toward_zero, uint32_t* status) {
    const uint32_t clean = _mm_getcsr() & ~HostFpEnvX86::kStickyFlags;
    _mm_setcsr(toward_zero ? clean | HostFpEnvX86::kRoundTowardZero : clean);
    const T r = Compute(op, a, b);
    *status   = _mm_getcsr();
    if (toward_zero) _mm_setcsr(clean);
    return r;
}
#endif

}

__declspec(noinline) float HostFpEnvMsvc::Run(HostFpOp op, float a, float b, bool toward_zero,
                                              uint32_t* status) {
    return RunWithFlags(op, a, b, toward_zero, status);
}

__declspec(noinline) double HostFpEnvMsvc::Run(HostFpOp op, double a, double b, bool toward_zero,
                                               uint32_t* status) {
    return RunWithFlags(op, a, b, toward_zero, status);
}

}

#endif
