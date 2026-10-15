#pragma once

#include <cmath>
#include <cstdint>
#include <xmmintrin.h>

#include "arm_vfp.h"

namespace cerfengine {

class ArmVfpArith {
public:
    static constexpr uint32_t kMxcsrOe     = 1u << 3;
    static constexpr uint32_t kMxcsrPe     = 1u << 5;
    static constexpr uint32_t kMxcsrSticky = 0x3Fu;

    /* Intel SDM Vol. 1 section 10.2.3.2 (p. 10-4): "Bits 13 and 14 of the
       MXCSR register (the rounding control [RC] field) control how the results
       of SIMD floating-point instructions are rounded". */
    static constexpr uint32_t kMxcsrRcZero = _MM_ROUND_TOWARD_ZERO;

    enum class Kind { Zero, Infinity, QNaN, SNaN, Nonzero };

    /* ARM DDI 0406C.c FPZero (p. A2-73), FPInfinity and FPDefaultNaN
       (p. A2-75): FPInfinity is sign:Ones(E):Zeros(F), FPDefaultNaN is
       '0':Ones(E):'1':Zeros(F-1). */
    struct Sp {
        using Flt  = float;
        using Bits = uint32_t;
        static constexpr Bits kSign  = 0x80000000u;
        static constexpr Bits kExp   = 0x7F800000u;
        static constexpr Bits kFrac  = 0x007FFFFFu;
        static constexpr Bits kQuiet = 0x00400000u;
        static constexpr Bits kDNaN  = 0x7FC00000u;
        static constexpr Bits kInf   = 0x7F800000u;
        static constexpr Bits kMinNo = 0x00800000u;
    };

    struct Dp {
        using Flt  = double;
        using Bits = uint64_t;
        static constexpr Bits kSign  = 0x8000000000000000ull;
        static constexpr Bits kExp   = 0x7FF0000000000000ull;
        static constexpr Bits kFrac  = 0x000FFFFFFFFFFFFFull;
        static constexpr Bits kQuiet = 0x0008000000000000ull;
        static constexpr Bits kDNaN  = 0x7FF8000000000000ull;
        static constexpr Bits kInf   = 0x7FF0000000000000ull;
        static constexpr Bits kMinNo = 0x0010000000000000ull;
    };

    template <class F>
    struct Val { Kind kind; typename F::Bits bits; bool sign; };

    template <class F>
    static inline typename F::Bits Zero(bool sign) {
        return sign ? F::kSign : typename F::Bits{};
    }

    template <class F>
    static inline typename F::Bits Inf(bool sign) {
        return sign ? (F::kInf | F::kSign) : F::kInf;
    }

    template <class F>
    static inline Val<F> Unpack(typename F::Bits b, uint32_t* fpscr) {
        const bool sign = (b & F::kSign) != 0;
        const typename F::Bits exp  = b & F::kExp;
        const typename F::Bits frac = b & F::kFrac;
        if (exp == 0) {
            if (frac == 0) return Val<F>{ Kind::Zero, b, sign };
            if ((*fpscr & ArmVfp::kFpscrFzMask) != 0u) {
                *fpscr |= ArmVfp::kFpscrIdcMask;
                return Val<F>{ Kind::Zero, Zero<F>(sign), sign };
            }
            return Val<F>{ Kind::Nonzero, b, sign };
        }
        if (exp == F::kExp) {
            if (frac == 0) return Val<F>{ Kind::Infinity, b, sign };
            return Val<F>{ (frac & F::kQuiet) != 0 ? Kind::QNaN : Kind::SNaN,
                           b, sign };
        }
        return Val<F>{ Kind::Nonzero, b, sign };
    }

    /* ARM DDI 0406C.c FPProcessNaN() (p. A2-77): "if type == FPType_SNaN then
       result<topfrac> = '1'; FPProcessException(FPExc_InvalidOp, fpscr_val);
       if fpscr_val<25> == '1' then result = FPDefaultNaN(N)". */
    template <class F>
    static inline typename F::Bits ProcessNaN(const Val<F>& a, uint32_t* fpscr) {
        typename F::Bits r = a.bits;
        if (a.kind == Kind::SNaN) {
            r |= F::kQuiet;
            *fpscr |= ArmVfp::kFpscrIocMask;
        }
        if ((*fpscr & ArmVfp::kFpscrDnMask) != 0u) r = F::kDNaN;
        return r;
    }

    /* ARM DDI 0406C.c FPProcessNaNs() (p. A2-77): SNaN op1, else SNaN op2,
       else QNaN op1, else QNaN op2. */
    template <class F>
    static inline bool ProcessNaNs(const Val<F>& a, const Val<F>& b,
                                   uint32_t* fpscr, typename F::Bits* out) {
        if (a.kind == Kind::SNaN) { *out = ProcessNaN<F>(a, fpscr); return true; }
        if (b.kind == Kind::SNaN) { *out = ProcessNaN<F>(b, fpscr); return true; }
        if (a.kind == Kind::QNaN) { *out = ProcessNaN<F>(a, fpscr); return true; }
        if (b.kind == Kind::QNaN) { *out = ProcessNaN<F>(b, fpscr); return true; }
        return false;
    }

    template <class F, class Fn>
    static inline typename F::Bits Numeric(uint32_t* fpscr, Fn compute) {
        const uint32_t csr = _mm_getcsr();
        _mm_setcsr(csr & ~kMxcsrSticky);
        const typename F::Flt r = compute();
        uint32_t raised = _mm_getcsr() & kMxcsrSticky;
        typename F::Bits rb = ArmVfp::BitCast<typename F::Bits>(r);
        const typename F::Bits mag = rb & ~F::kSign;
        const bool inexact = (raised & kMxcsrPe) != 0u;
        bool tiny = mag < F::kMinNo && (mag != 0 || inexact);
        if (mag == F::kMinNo && inexact) {
            _mm_setcsr((csr & ~kMxcsrSticky) | kMxcsrRcZero);
            const typename F::Bits zb = ArmVfp::BitCast<typename F::Bits>(compute()) & ~F::kSign;
            _mm_setcsr(csr & ~kMxcsrSticky);
            tiny = zb < F::kMinNo;
        }
        if (tiny && (*fpscr & ArmVfp::kFpscrFzMask) != 0u) {
            rb &= F::kSign;
            *fpscr |= ArmVfp::kFpscrUfcMask;
            raised &= ~kMxcsrPe;
        } else if (tiny && inexact) {
            *fpscr |= ArmVfp::kFpscrUfcMask;
        }
        if (raised & kMxcsrOe) *fpscr |= ArmVfp::kFpscrOfcMask;
        if (raised & kMxcsrPe) *fpscr |= ArmVfp::kFpscrIxcMask;
        return rb;
    }

    /* ARM DDI 0406C.c FPAdd() (p. A2-82). */
    template <class F>
    static inline typename F::Bits Add(typename F::Bits x, typename F::Bits y,
                                       uint32_t* fpscr) {
        const Val<F> a = Unpack<F>(x, fpscr);
        const Val<F> b = Unpack<F>(y, fpscr);
        typename F::Bits nan;
        if (ProcessNaNs<F>(a, b, fpscr, &nan)) return nan;
        const bool i1 = a.kind == Kind::Infinity, i2 = b.kind == Kind::Infinity;
        const bool z1 = a.kind == Kind::Zero,     z2 = b.kind == Kind::Zero;
        if (i1 && i2 && a.sign != b.sign) {
            *fpscr |= ArmVfp::kFpscrIocMask;
            return F::kDNaN;
        }
        if ((i1 && !a.sign) || (i2 && !b.sign)) return Inf<F>(false);
        if ((i1 && a.sign)  || (i2 && b.sign))  return Inf<F>(true);
        if (z1 && z2 && a.sign == b.sign)       return Zero<F>(a.sign);
        return Numeric<F>(fpscr, [&] {
            return ArmVfp::BitCast<typename F::Flt>(a.bits) + ArmVfp::BitCast<typename F::Flt>(b.bits);
        });
    }

    /* ARM DDI 0406C.c FPSub() (p. A2-82). */
    template <class F>
    static inline typename F::Bits Sub(typename F::Bits x, typename F::Bits y,
                                       uint32_t* fpscr) {
        const Val<F> a = Unpack<F>(x, fpscr);
        const Val<F> b = Unpack<F>(y, fpscr);
        typename F::Bits nan;
        if (ProcessNaNs<F>(a, b, fpscr, &nan)) return nan;
        const bool i1 = a.kind == Kind::Infinity, i2 = b.kind == Kind::Infinity;
        const bool z1 = a.kind == Kind::Zero,     z2 = b.kind == Kind::Zero;
        if (i1 && i2 && a.sign == b.sign) {
            *fpscr |= ArmVfp::kFpscrIocMask;
            return F::kDNaN;
        }
        if ((i1 && !a.sign) || (i2 && b.sign))  return Inf<F>(false);
        if ((i1 && a.sign)  || (i2 && !b.sign)) return Inf<F>(true);
        if (z1 && z2 && a.sign != b.sign)       return Zero<F>(a.sign);
        return Numeric<F>(fpscr, [&] {
            return ArmVfp::BitCast<typename F::Flt>(a.bits) - ArmVfp::BitCast<typename F::Flt>(b.bits);
        });
    }

    /* ARM DDI 0406C.c FPMul() (p. A2-83). */
    template <class F>
    static inline typename F::Bits Mul(typename F::Bits x, typename F::Bits y,
                                       uint32_t* fpscr) {
        const Val<F> a = Unpack<F>(x, fpscr);
        const Val<F> b = Unpack<F>(y, fpscr);
        typename F::Bits nan;
        if (ProcessNaNs<F>(a, b, fpscr, &nan)) return nan;
        const bool i1 = a.kind == Kind::Infinity, i2 = b.kind == Kind::Infinity;
        const bool z1 = a.kind == Kind::Zero,     z2 = b.kind == Kind::Zero;
        const bool rs = a.sign != b.sign;
        if ((i1 && z2) || (z1 && i2)) {
            *fpscr |= ArmVfp::kFpscrIocMask;
            return F::kDNaN;
        }
        if (i1 || i2) return Inf<F>(rs);
        if (z1 || z2) return Zero<F>(rs);
        return Numeric<F>(fpscr, [&] {
            return ArmVfp::BitCast<typename F::Flt>(a.bits) * ArmVfp::BitCast<typename F::Flt>(b.bits);
        });
    }

    /* ARM DDI 0406C.c FPDiv() (pp. A2-83, A2-84), including "if !inf1 then
       FPProcessException(FPExc_DivideByZero, fpscr_val)". */
    template <class F>
    static inline typename F::Bits Div(typename F::Bits x, typename F::Bits y,
                                       uint32_t* fpscr) {
        const Val<F> a = Unpack<F>(x, fpscr);
        const Val<F> b = Unpack<F>(y, fpscr);
        typename F::Bits nan;
        if (ProcessNaNs<F>(a, b, fpscr, &nan)) return nan;
        const bool i1 = a.kind == Kind::Infinity, i2 = b.kind == Kind::Infinity;
        const bool z1 = a.kind == Kind::Zero,     z2 = b.kind == Kind::Zero;
        const bool rs = a.sign != b.sign;
        if ((i1 && i2) || (z1 && z2)) {
            *fpscr |= ArmVfp::kFpscrIocMask;
            return F::kDNaN;
        }
        if (i1 || z2) {
            if (!i1) *fpscr |= ArmVfp::kFpscrDzcMask;
            return Inf<F>(rs);
        }
        if (z1 || i2) return Zero<F>(rs);
        return Numeric<F>(fpscr, [&] {
            return ArmVfp::BitCast<typename F::Flt>(a.bits) / ArmVfp::BitCast<typename F::Flt>(b.bits);
        });
    }

    template <class F>
    static inline typename F::Bits Sqrt(typename F::Bits x, uint32_t* fpscr) {
        const Val<F> a = Unpack<F>(x, fpscr);
        if (a.kind == Kind::SNaN || a.kind == Kind::QNaN) {
            return ProcessNaN<F>(a, fpscr);
        }
        if (a.kind == Kind::Zero) return Zero<F>(a.sign);
        if (a.kind == Kind::Infinity && !a.sign) return Inf<F>(false);
        if (a.sign) {
            *fpscr |= ArmVfp::kFpscrIocMask;
            return F::kDNaN;
        }
        return Numeric<F>(fpscr, [&] {
            return std::sqrt(ArmVfp::BitCast<typename F::Flt>(a.bits));
        });
    }

    /* ARM DDI 0406C.c FPNeg() (p. A2-75): negation "only affect the sign bit",
       and does not treat NaN or denormalized operands specially. */
    template <class F>
    static inline typename F::Bits Neg(typename F::Bits x) {
        return x ^ F::kSign;
    }

    template <class F>
    static inline typename F::Bits DataOp(uint32_t key, typename F::Bits d,
                                          typename F::Bits n, typename F::Bits m,
                                          uint32_t* fpscr) {
        switch (key) {
        case 0: return Add<F>(d, Mul<F>(n, m, fpscr), fpscr);
        case 1: return Add<F>(d, Neg<F>(Mul<F>(n, m, fpscr)), fpscr);
        case 2: return Add<F>(Neg<F>(d), Mul<F>(n, m, fpscr), fpscr);
        case 3: return Add<F>(Neg<F>(d), Neg<F>(Mul<F>(n, m, fpscr)), fpscr);
        case 4: return Mul<F>(n, m, fpscr);
        case 5: return Neg<F>(Mul<F>(n, m, fpscr));
        case 6: return Add<F>(n, m, fpscr);
        default: return Sub<F>(n, m, fpscr);
        }
    }
};

}
