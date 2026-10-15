#pragma once

#include <cerfengine/calling_convention.h>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmVfp : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    /* FPSCR.QC bit[27] - sticky Advanced SIMD saturation flag (B4.1.58). */
    static constexpr uint32_t kFpscrQcMask = 1u << 27;

    /* ARM DDI 0406C.c B4.1.58: cumulative exception bits IOC[0], DZC[1],
       OFC[2], UFC[3], IXC[4], IDC[7] (p. B4-1572); FZ bit[24], DN bit[25] and
       AHP bit[26] "Alternative half-precision control bit" (p. B4-1571). */
    static constexpr uint32_t kFpscrIocMask = 1u;
    static constexpr uint32_t kFpscrDzcMask = 1u << 1;
    static constexpr uint32_t kFpscrOfcMask = 1u << 2;
    static constexpr uint32_t kFpscrUfcMask = 1u << 3;
    static constexpr uint32_t kFpscrIxcMask = 1u << 4;
    static constexpr uint32_t kFpscrIdcMask = 1u << 7;
    static constexpr uint32_t kFpscrFzMask  = 1u << 24;
    static constexpr uint32_t kFpscrDnMask  = 1u << 25;
    static constexpr uint32_t kFpscrAhpMask = 1u << 26;

    /* ARM DDI 0406C.c B4.1.58 (p. B4-1571): "Bits[15, 12:8] Floating-point
       exception trap enable bits" IDE, IXE, UFE, OFE, DZE, IOE. */
    static constexpr uint32_t kFpscrTrapEnableMask = 0x9F00u;

    template <class To, class From>
    static inline To BitCast(From value) {
        static_assert(sizeof(To) == sizeof(From));
        To result;
        std::memcpy(&result, &value, sizeof result);
        return result;
    }

    static inline float FlushDenormalS(float f, uint32_t* fpscr) {
        uint32_t b;
        std::memcpy(&b, &f, 4);
        if ((b & 0x7F800000u) == 0u && (b & 0x007FFFFFu) != 0u) {
            *fpscr |= kFpscrIdcMask;
            return 0.0f;
        }
        return f;
    }

    static inline double FlushDenormalD(double d, uint32_t* fpscr) {
        uint64_t b;
        std::memcpy(&b, &d, 8);
        if ((b & 0x7FF0000000000000ull) == 0ull &&
            (b & 0x000FFFFFFFFFFFFFull) != 0ull) {
            *fpscr |= kFpscrIdcMask;
            return 0.0;
        }
        return d;
    }

    static inline uint32_t FPToFixed32(double src, bool is_signed,
                                       bool round_zero, uint32_t rmode,
                                       uint32_t* fpscr) {
        if (std::isnan(src)) {
            *fpscr |= kFpscrIocMask;
            return 0u;
        }
        const uint32_t mode = round_zero ? 3u : (rmode & 3u);
        const double f = std::floor(src);
        const double err = src - f;
        bool round_up;
        switch (mode) {
            case 1u:  round_up = (err != 0.0);                       break;
            case 2u:  round_up = false;                              break;
            case 3u:  round_up = (err != 0.0 && f < 0.0);            break;
            default:  round_up = (err > 0.5) ||
                                 (err == 0.5 && std::fmod(f, 2.0) != 0.0);
                      break;
        }
        const double v = std::isinf(src) ? src : (round_up ? f + 1.0 : f);
        const bool inexact = (err != 0.0);
        const double hi = is_signed ? 2147483648.0 : 4294967296.0;
        const double lo = is_signed ? -2147483648.0 : 0.0;
        if (v >= hi) {
            *fpscr |= kFpscrIocMask;
            return is_signed ? 0x7FFFFFFFu : 0xFFFFFFFFu;
        }
        if (v < lo) {
            *fpscr |= kFpscrIocMask;
            return is_signed ? 0x80000000u : 0u;
        }
        if (inexact) *fpscr |= kFpscrIxcMask;
        return is_signed ? static_cast<uint32_t>(static_cast<int32_t>(v))
                         : static_cast<uint32_t>(v);
    }

    static inline float FixedToFP32(uint32_t mag, bool negative,
                                    uint32_t rmode, uint32_t* fpscr) {
        if (mag == 0u) return 0.0f;
        uint32_t  q     = mag;
        int       scale = 0;
        const int width = static_cast<int>(std::bit_width(mag));
        if (width > 24) {
            const int      shift = width - 24;
            const uint32_t rem   = mag & ((1u << shift) - 1u);
            const uint32_t half  = 1u << (shift - 1);
            q = mag >> shift;
            bool round_up;
            switch (rmode & 3u) {
                case 1u: round_up = (rem != 0u && !negative); break;
                case 2u: round_up = (rem != 0u && negative);  break;
                case 3u: round_up = false;                    break;
                default: round_up = (rem > half) ||
                                    (rem == half && (q & 1u) != 0u);
                         break;
            }
            if (round_up) ++q;
            scale = shift;
            if (rem != 0u) *fpscr |= kFpscrIxcMask;
        }
        const float f = std::ldexp(static_cast<float>(q), scale);
        return negative ? -f : f;
    }

    static inline float FPDoubleToSingle32(double src, uint32_t* fpscr) {
        uint64_t b;
        std::memcpy(&b, &src, 8);
        const uint32_t sign = static_cast<uint32_t>(b >> 63) << 31;
        const bool fz = (*fpscr & kFpscrFzMask) != 0u;
        if (std::isnan(src)) {
            if ((b & 0x0008000000000000ull) == 0ull) *fpscr |= kFpscrIocMask;
            if ((*fpscr & kFpscrDnMask) != 0u) return BitCast<float>(0x7FC00000u);
            return BitCast<float>(sign | 0x7FC00000u |
                                  static_cast<uint32_t>((b >> 29) & 0x3FFFFFu));
        }
        if (std::isinf(src)) return BitCast<float>(sign | 0x7F800000u);
        const double in = fz ? FlushDenormalD(src, fpscr) : src;
        if (in == 0.0) return BitCast<float>(sign);

        const double mag = std::fabs(in);
        if (fz && mag < static_cast<double>(FLT_MIN)) {
            *fpscr |= kFpscrUfcMask;
            return BitCast<float>(sign);
        }
        const float  r  = static_cast<float>(in);
        const double rd = static_cast<double>(r);
        const bool inexact = (rd != in);
        const bool overflow = std::isinf(rd) || mag >= 0x1p128;
        if (overflow) *fpscr |= kFpscrOfcMask;
        if (!overflow && mag < static_cast<double>(FLT_MIN) && inexact) {
            *fpscr |= kFpscrUfcMask;
        }
        if (inexact || overflow) *fpscr |= kFpscrIxcMask;
        return r;
    }

    /* ARM DDI 0406C.c FPSingleToDouble() (p. A2-91): same NaN and DN handling,
       else "FPRound(value, 64, fpscr_val); // Rounding will be exact". */
    static inline double FPSingleToDouble64(float src, uint32_t* fpscr) {
        uint32_t b;
        std::memcpy(&b, &src, 4);
        const uint64_t sign = static_cast<uint64_t>(b >> 31) << 63;
        if (std::isnan(src)) {
            if ((b & 0x00400000u) == 0u) *fpscr |= kFpscrIocMask;
            if ((*fpscr & kFpscrDnMask) != 0u) {
                return BitCast<double>(0x7FF8000000000000ull);
            }
            return BitCast<double>(sign | 0x7FF8000000000000ull |
                                   (static_cast<uint64_t>(b & 0x3FFFFFu) << 29));
        }
        if (std::isinf(src)) {
            return BitCast<double>(sign | 0x7FF0000000000000ull);
        }
        const float in =
            ((*fpscr & kFpscrFzMask) != 0u) ? FlushDenormalS(src, fpscr) : src;
        if (in == 0.0f) return BitCast<double>(sign);
        return static_cast<double>(in);
    }

    static inline float  FPAddS (float a, float b)  { return a + b; }
    static inline float  FPSubS (float a, float b)  { return a - b; }
    static inline float  FPMulS (float a, float b)  { return a * b; }
    static inline float  FPDivS (float a, float b)  { return a / b; }
    static inline float  FPNegS (float a)           { return -a; }
    static inline float  FPAbsS (float a)           { return std::fabs(a); }
    static inline float  FPSqrtS(float a)           { return std::sqrt(a); }
    static inline float  FPFmaS (float a, float b, float c) { return std::fma(a, b, c); }

    static inline bool IsZeroOrDenormS(float a) {
        uint32_t bits;
        std::memcpy(&bits, &a, 4);
        return (bits & 0x7F800000u) == 0u;
    }

    static inline float RecipStepProductS(float op1, float op2) {
        const bool inf1 = std::isinf(op1);
        const bool inf2 = std::isinf(op2);
        const bool zd1  = IsZeroOrDenormS(op1);
        const bool zd2  = IsZeroOrDenormS(op2);
        if ((inf1 && zd2) || (zd1 && inf2)) return 0.0f;
        return op1 * op2;
    }

    static inline float FPRecipStepS(float op1, float op2) {
        return 2.0f - RecipStepProductS(op1, op2);
    }
    static inline float FPRSqrtStepS(float op1, float op2) {
        return (3.0f - RecipStepProductS(op1, op2)) * 0.5f;
    }

    static inline double FPAddD (double a, double b){ return a + b; }
    static inline double FPSubD (double a, double b){ return a - b; }
    static inline double FPMulD (double a, double b){ return a * b; }
    static inline double FPDivD (double a, double b){ return a / b; }
    static inline double FPNegD (double a)          { return -a; }
    static inline double FPAbsD (double a)          { return std::fabs(a); }
    static inline double FPSqrtD(double a)          { return std::sqrt(a); }

    static inline bool FPCompareEqS(float a, float b)  { return a == b; }
    static inline bool FPCompareGtS(float a, float b)  { return a >  b; }
    static inline bool FPCompareGeS(float a, float b)  { return a >= b; }
    static inline bool FPCompareLtS(float a, float b)  { return a <  b; }
    static inline bool FPCompareLeS(float a, float b)  { return a <= b; }

    static inline bool FPCompareEqD(double a, double b){ return a == b; }
    static inline bool FPCompareGtD(double a, double b){ return a >  b; }
    static inline bool FPCompareLtD(double a, double b){ return a <  b; }

    /* IEEE FPMax / FPMin: any-NaN → NaN; equal-value tiebreak uses sign
       (max prefers +0, min prefers -0) per ARM ARM FPMax / FPMin spec. */
    static inline float FPMaxS(float a, float b) {
        if (std::isnan(a) || std::isnan(b)) return std::nanf("");
        if (a > b) return a;
        if (b > a) return b;
        uint32_t ba, bb;
        std::memcpy(&ba, &a, 4);
        std::memcpy(&bb, &b, 4);
        const uint32_t r = ba & bb;
        float result;
        std::memcpy(&result, &r, 4);
        return result;
    }
    static inline float FPMinS(float a, float b) {
        if (std::isnan(a) || std::isnan(b)) return std::nanf("");
        if (a < b) return a;
        if (b < a) return b;
        uint32_t ba, bb;
        std::memcpy(&ba, &a, 4);
        std::memcpy(&bb, &b, 4);
        const uint32_t r = ba | bb;
        float result;
        std::memcpy(&result, &r, 4);
        return result;
    }

    static inline int64_t LoadIntS(const uint8_t* p, uint32_t esize) {
        if (esize == 8u)  { int8_t  v; std::memcpy(&v, p, 1); return v; }
        if (esize == 16u) { int16_t v; std::memcpy(&v, p, 2); return v; }
        if (esize == 32u) { int32_t v; std::memcpy(&v, p, 4); return v; }
        int64_t v; std::memcpy(&v, p, 8); return v;
    }
    static inline uint64_t LoadIntU(const uint8_t* p, uint32_t esize) {
        if (esize == 8u)  { return *p; }
        if (esize == 16u) { uint16_t v; std::memcpy(&v, p, 2); return v; }
        if (esize == 32u) { uint32_t v; std::memcpy(&v, p, 4); return v; }
        uint64_t v; std::memcpy(&v, p, 8); return v;
    }

    /* ARM ARM A2.7 "Floating-point reciprocal estimate
       and step" / "square root estimate and step" (page A2-85 / A2-87).
       a ∈ [0.5, 1.0) for RecipEstimate; a ∈ [0.25, 1.0) for RecipSqrt. */
    static inline double RecipEstimate(double a) {
        int q = static_cast<int>(a * 512.0);
        double r = 1.0 / ((static_cast<double>(q) + 0.5) / 512.0);
        int s = static_cast<int>(256.0 * r + 0.5);
        return static_cast<double>(s) / 256.0;
    }
    static inline double RecipSqrtEstimate(double a) {
        int q;
        double r;
        if (a < 0.5) {
            q = static_cast<int>(a * 512.0);
            r = 1.0 / std::sqrt((static_cast<double>(q) + 0.5) / 512.0);
        } else {
            q = static_cast<int>(a * 256.0);
            r = 1.0 / std::sqrt((static_cast<double>(q) + 0.5) / 256.0);
        }
        int s = static_cast<int>(256.0 * r + 0.5);
        return static_cast<double>(s) / 256.0;
    }

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t ExecuteCdp(uint32_t pc, uint32_t packed);

    static uint32_t CERFENGINE_CDECL ExecuteCdpHelper(ArmVfp*  vfp,
                                                      uint32_t pc,
                                                      uint32_t packed);

private:
    int  host_rmode_       = 0;
    bool host_rmode_known_ = false;
    bool dp32_             = false;
    bool sp_vfpv3_         = false;
    bool dp_vfpv3_         = false;
};

}
