#if defined(_MSC_VER)
#pragma fenv_access(on)
#endif

#include "arm_vfp.h"
#include <cerfengine/calling_convention.h>

#include <cfenv>
#include <cmath>
#include <cstring>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include "arm_vfp_arith.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmVfp);

namespace {

struct FpOperand {
    double value;
    bool   is_nan;
    bool   is_snan;
};

FpOperand VfpUnpackS(float f, uint32_t* fpscr) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    const bool nan  = (b & 0x7F800000u) == 0x7F800000u &&
                      (b & 0x007FFFFFu) != 0u;
    const bool snan = nan && (b & 0x00400000u) == 0u;
    float v = f;
    if (!nan && (*fpscr & ArmVfp::kFpscrFzMask) != 0u) {
        v = ArmVfp::FlushDenormalS(f, fpscr);
    }
    return FpOperand{ static_cast<double>(v), nan, snan };
}

FpOperand VfpUnpackD(double d, uint32_t* fpscr) {
    uint64_t b;
    std::memcpy(&b, &d, 8);
    const bool nan  = (b & 0x7FF0000000000000ull) == 0x7FF0000000000000ull &&
                      (b & 0x000FFFFFFFFFFFFFull) != 0ull;
    const bool snan = nan && (b & 0x0008000000000000ull) == 0ull;
    double v = d;
    if (!nan && (*fpscr & ArmVfp::kFpscrFzMask) != 0u) {
        v = ArmVfp::FlushDenormalD(d, fpscr);
    }
    return FpOperand{ v, nan, snan };
}

/* ARM DDI 0406C.c FPCompare() (p. A2-80): any NaN operand gives
   ('0','0','1','1') and "if type1==FPType_SNaN || type2==FPType_SNaN ||
   quiet_nan_exc then FPProcessException(FPExc_InvalidOp)". */
uint32_t VfpCompareNzcv(const FpOperand& a, const FpOperand& b,
                        bool quiet_nan_exc, uint32_t* fpscr) {
    if (a.is_nan || b.is_nan) {
        if (a.is_snan || b.is_snan || quiet_nan_exc) {
            *fpscr |= ArmVfp::kFpscrIocMask;
        }
        return 0x3u;
    }
    if (a.value == b.value) return 0x6u;
    if (a.value <  b.value) return 0x8u;
    return 0x2u;
}

inline void StoreFpscrNzcv(ArmCpuState* state, uint32_t nzcv4) {
    state->fpscr = (state->fpscr & ~0xF0000000u) | (nzcv4 << 28);
}

uint32_t VfpVectorRegs(const ArmCpuState* state, bool is_dp, bool dp32,
                       bool monadic,
                       uint32_t sd0, uint32_t sn0, uint32_t sm0,
                       uint32_t* sd, uint32_t* sn, uint32_t* sm) {
    const uint32_t len    = ((state->fpscr >> 16) & 7u) + 1u;
    const uint32_t stride = ((state->fpscr >> 20) & 3u) == 0u ? 1u : 2u;
    const uint32_t bank   = is_dp ? 4u : 8u;
    const uint32_t d_bank = sd0 / bank;
    if (len == 1u || d_bank == 0u || (is_dp && dp32 && d_bank == 4u)) {
        sd[0] = sd0; sn[0] = sn0; sm[0] = sm0;
        return 1u;
    }
    const uint32_t m_bank   = sm0 / bank;
    const bool     m_scalar = m_bank == 0u || (is_dp && dp32 && m_bank == 4u);
    const uint32_t d_base = sd0 / bank * bank, d_i0 = sd0 % bank;
    const uint32_t n_base = sn0 / bank * bank, n_i0 = sn0 % bank;
    const uint32_t m_base = sm0 / bank * bank, m_i0 = sm0 % bank;
    for (uint32_t i = 0; i < len; ++i) {
        sd[i] = d_base + (d_i0 + i * stride) % bank;
        sn[i] = monadic ? sn0 : n_base + (n_i0 + i * stride) % bank;
        sm[i] = m_scalar ? sm0 : m_base + (m_i0 + i * stride) % bank;
    }
    return len;
}

constexpr int kHostRoundingFor[4] = { FE_TONEAREST, FE_UPWARD,
                                      FE_DOWNWARD, FE_TOWARDZERO };

class HostRoundingMode {
public:
    HostRoundingMode(uint32_t fpscr, int* host_rmode)
        : host_rmode_(host_rmode),
          want_(kHostRoundingFor[(fpscr >> 22) & 3u]),
          saved_(*host_rmode) {
        if (want_ != saved_) {
            std::fesetround(want_);
            *host_rmode_ = want_;
        }
    }
    ~HostRoundingMode() {
        if (want_ != saved_) {
            std::fesetround(saved_);
            *host_rmode_ = saved_;
        }
    }

    HostRoundingMode(const HostRoundingMode&)            = delete;
    HostRoundingMode& operator=(const HostRoundingMode&) = delete;

private:
    int* host_rmode_;
    int  want_;
    int  saved_;
};

}

bool ArmVfp::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void ArmVfp::OnReady() {
    const uint32_t mvfr0 = engine_.GetSetup().arm.mvfr0;
    dp32_     = (mvfr0 & 0xFu) == 0x2u;
    sp_vfpv3_ = ((mvfr0 >> 4) & 0xFu) >= 0x2u;
    dp_vfpv3_ = ((mvfr0 >> 8) & 0xFu) >= 0x2u;
}

uint32_t ArmVfp::ExecuteCdp(uint32_t pc, uint32_t packed) {
    auto& cpu = engine_.Get<ArmCpu>();
    auto* state = cpu.State();

    const uint32_t crm    =  packed        & 0xFu;
    const uint32_t cp_bits= (packed >> 4)  & 0x7u;
    const uint32_t cp_num = (packed >> 7)  & 0xFu;
    const uint32_t crd    = (packed >> 11) & 0xFu;
    const uint32_t crn    = (packed >> 15) & 0xFu;
    const uint32_t cp_opc = (packed >> 19) & 0xFu;

    if (cp_num != 10u && cp_num != 11u) {
        cpu.RaiseUndefinedException(pc);
        return 1;
    }
    /* DDI 0406C.c FPProcessException() (p. A2-77): with a trap enable set the
       spec takes "IMPLEMENTATION_DEFINED floating-point trap handling" instead
       of writing the cumulative bit. */
    if ((state->fpscr & kFpscrTrapEnableMask) != 0u) {
        engine_.Halt(cerfengine::HaltReason::kUnimplemented,
            "VFP floating-point trap handling (FPSCR=0x%08X) not implemented "
            "at guest pc=0x%08X", state->fpscr, pc);
    }
    if (!host_rmode_known_) {
        std::fesetround(FE_TONEAREST);
        host_rmode_       = FE_TONEAREST;
        host_rmode_known_ = true;
    }
    const HostRoundingMode rounding(state->fpscr, &host_rmode_);
    const bool is_dp = (cp_num == 11u);

    const uint32_t T   = (cp_opc >> 3) & 1u;
    const uint32_t D   = (cp_opc >> 2) & 1u;
    const uint32_t opc =  cp_opc       & 3u;
    const uint32_t N   = (cp_bits >> 2) & 1u;
    const uint32_t op6 = (cp_bits >> 1) & 1u;
    const uint32_t M   =  cp_bits       & 1u;

    uint32_t sd, sn, sm;
    if (is_dp) {
        sd = (D << 4) | crd;
        sn = (N << 4) | crn;
        sm = (M << 4) | crm;
    } else {
        sd = (crd << 1) | D;
        sn = (crn << 1) | N;
        sm = (crm << 1) | M;
    }

    float*  sp_regs = reinterpret_cast<float*> (state->vfp_d);
    double* dp_regs = reinterpret_cast<double*>(state->vfp_d);

    if (T == 0u) {
        const uint32_t key = (opc << 1) | op6;
        uint32_t vd[8], vn[8], vm[8];
        const uint32_t vl = VfpVectorRegs(state, is_dp, dp32_, false, sd, sn, sm,
                                          vd, vn, vm);
        for (uint32_t i = 0; i < vl; ++i) {
            if (is_dp) {
                using F = ArmVfpArith::Dp;
                uint64_t d, n, m;
                std::memcpy(&d, &dp_regs[vd[i]], 8);
                std::memcpy(&n, &dp_regs[vn[i]], 8);
                std::memcpy(&m, &dp_regs[vm[i]], 8);
                const uint64_t r =
                    ArmVfpArith::DataOp<F>(key, d, n, m, &state->fpscr);
                std::memcpy(&dp_regs[vd[i]], &r, 8);
            } else {
                using F = ArmVfpArith::Sp;
                uint32_t d, n, m;
                std::memcpy(&d, &sp_regs[vd[i]], 4);
                std::memcpy(&n, &sp_regs[vn[i]], 4);
                std::memcpy(&m, &sp_regs[vm[i]], 4);
                const uint32_t r =
                    ArmVfpArith::DataOp<F>(key, d, n, m, &state->fpscr);
                std::memcpy(&sp_regs[vd[i]], &r, 4);
            }
        }
        return 0;
    }

    if (opc == 0u && op6 == 0u) {
        uint32_t vd[8], vn[8], vm[8];
        const uint32_t vl = VfpVectorRegs(state, is_dp, dp32_, false, sd, sn, sm,
                                          vd, vn, vm);
        for (uint32_t i = 0; i < vl; ++i) {
            if (is_dp) {
                uint64_t n, m;
                std::memcpy(&n, &dp_regs[vn[i]], 8);
                std::memcpy(&m, &dp_regs[vm[i]], 8);
                const uint64_t r = ArmVfpArith::Div<ArmVfpArith::Dp>(
                    n, m, &state->fpscr);
                std::memcpy(&dp_regs[vd[i]], &r, 8);
            } else {
                uint32_t n, m;
                std::memcpy(&n, &sp_regs[vn[i]], 4);
                std::memcpy(&m, &sp_regs[vm[i]], 4);
                const uint32_t r = ArmVfpArith::Div<ArmVfpArith::Sp>(
                    n, m, &state->fpscr);
                std::memcpy(&sp_regs[vd[i]], &r, 4);
            }
        }
        return 0;
    }

    if (opc != 3u) {
        if ((opc == 1u || opc == 2u) &&
            ((engine_.GetSetup().arm.mvfr1 >> 28) & 0xFu) != 0u) {
            engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                "VFP fused multiply-accumulate (opc1=0x%X, cp%u) not "
                "implemented at guest pc=0x%08X", cp_opc, cp_num, pc);
        }
        cpu.RaiseUndefinedException(pc);
        return 1;
    }

    const uint32_t bit7 = (cp_bits >> 2) & 1u;
    const uint32_t bit6 =  op6;
    const uint32_t op_sel = (bit7 << 1) | bit6;

    if (op6 == 0u) {
        /* A8.8.339 (p. A8-936): "Encoding T2/A2  VFPv3, VFPv4 (sz = 1
           UNDEFINED in single-precision only variants)". */
        if (!(is_dp ? dp_vfpv3_ : sp_vfpv3_)) {
            cpu.RaiseUndefinedException(pc);
            return 1;
        }
        const uint32_t imm8 = (crn << 4) | crm;
        const uint32_t a = (imm8 >> 7) & 1u;
        const uint32_t b = (imm8 >> 6) & 1u;
        const uint32_t cdef = imm8 & 0x3Fu;
        uint32_t vd[8], vn[8], vm[8];
        const uint32_t vl = VfpVectorRegs(state, is_dp, dp32_, true, sd, sd, sd,
                                          vd, vn, vm);
        if (is_dp) {
            uint64_t bits = (static_cast<uint64_t>(a) << 63)
                          | (static_cast<uint64_t>(!b ? 1u : 0u) << 62)
                          | (static_cast<uint64_t>(b ? 0xFFu : 0u) << 54)
                          | (static_cast<uint64_t>(cdef) << 48);
            for (uint32_t i = 0; i < vl; ++i) {
                std::memcpy(&dp_regs[vd[i]], &bits, 8);
            }
        } else {
            uint32_t bits = (a << 31)
                          | ((!b ? 1u : 0u) << 30)
                          | ((b ? 0x1Fu : 0u) << 25)
                          | (cdef << 19);
            for (uint32_t i = 0; i < vl; ++i) {
                std::memcpy(&sp_regs[vd[i]], &bits, 4);
            }
        }
        return 0;
    }

    switch (crn) {
        case 0x0: {
            if (op_sel == 1u) {
                uint32_t vd[8], vn[8], vm[8];
                const uint32_t vl = VfpVectorRegs(state, is_dp, dp32_, true, sd, sn, sm,
                                                  vd, vn, vm);
                for (uint32_t i = 0; i < vl; ++i) {
                    if (is_dp) dp_regs[vd[i]] = dp_regs[vm[i]];
                    else       sp_regs[vd[i]] = sp_regs[vm[i]];
                }
                return 0;
            }
            if (op_sel == 3u) {
                uint32_t vd[8], vn[8], vm[8];
                const uint32_t vl = VfpVectorRegs(state, is_dp, dp32_, true, sd, sn, sm,
                                                  vd, vn, vm);
                for (uint32_t i = 0; i < vl; ++i) {
                    if (is_dp) dp_regs[vd[i]] = FPAbsD(dp_regs[vm[i]]);
                    else       sp_regs[vd[i]] = FPAbsS(sp_regs[vm[i]]);
                }
                return 0;
            }
            cpu.RaiseUndefinedException(pc);
            return 1;
        }
        case 0x1: {
            if (op_sel == 1u) {
                uint32_t vd[8], vn[8], vm[8];
                const uint32_t vl = VfpVectorRegs(state, is_dp, dp32_, true, sd, sn, sm,
                                                  vd, vn, vm);
                for (uint32_t i = 0; i < vl; ++i) {
                    if (is_dp) dp_regs[vd[i]] = FPNegD(dp_regs[vm[i]]);
                    else       sp_regs[vd[i]] = FPNegS(sp_regs[vm[i]]);
                }
                return 0;
            }
            if (op_sel == 3u) {
                uint32_t vd[8], vn[8], vm[8];
                const uint32_t vl = VfpVectorRegs(state, is_dp, dp32_, true, sd, sn, sm,
                                                  vd, vn, vm);
                for (uint32_t i = 0; i < vl; ++i) {
                    if (is_dp) {
                        uint64_t m;
                        std::memcpy(&m, &dp_regs[vm[i]], 8);
                        const uint64_t r = ArmVfpArith::Sqrt<ArmVfpArith::Dp>(
                            m, &state->fpscr);
                        std::memcpy(&dp_regs[vd[i]], &r, 8);
                    } else {
                        uint32_t m;
                        std::memcpy(&m, &sp_regs[vm[i]], 4);
                        const uint32_t r = ArmVfpArith::Sqrt<ArmVfpArith::Sp>(
                            m, &state->fpscr);
                        std::memcpy(&sp_regs[vd[i]], &r, 4);
                    }
                }
                return 0;
            }
            cpu.RaiseUndefinedException(pc);
            return 1;
        }
        case 0x4:
        case 0x5: {
            /* DDI 0406C.c A8.8.303 (p. A8-864): "quiet_nan_exc = (E == '1')",
               E is bit[7]; crn 0x5 is the compare-with-zero form, whose second
               operand FPUnpack yields FPType_Zero. */
            const FpOperand a =
                is_dp ? VfpUnpackD(dp_regs[sd], &state->fpscr)
                      : VfpUnpackS(sp_regs[sd], &state->fpscr);
            const FpOperand b =
                (crn == 0x5u) ? FpOperand{ 0.0, false, false }
                              : (is_dp ? VfpUnpackD(dp_regs[sm], &state->fpscr)
                                       : VfpUnpackS(sp_regs[sm], &state->fpscr));
            StoreFpscrNzcv(state,
                VfpCompareNzcv(a, b, bit7 != 0u, &state->fpscr));
            return 0;
        }
        case 0x7: {
            if (op_sel == 3u) {
                if (is_dp) {
                    sp_regs[(crd << 1) | D] = FPDoubleToSingle32(
                        dp_regs[(M << 4) | crm], &state->fpscr);
                } else {
                    dp_regs[(D << 4) | crd] = FPSingleToDouble64(
                        sp_regs[(crm << 1) | M], &state->fpscr);
                }
                return 0;
            }
            cpu.RaiseUndefinedException(pc);
            return 1;
        }
        case 0x8: {
            const uint32_t src_sp_idx = (crm << 1) | M;
            uint32_t src_int;
            std::memcpy(&src_int, &sp_regs[src_sp_idx], 4);
            const bool is_signed = (bit7 != 0u);
            if (is_dp) {
                dp_regs[sd] = is_signed
                    ? static_cast<double>(static_cast<int32_t>(src_int))
                    : static_cast<double>(src_int);
            } else {
                const bool neg =
                    is_signed && static_cast<int32_t>(src_int) < 0;
                const uint32_t mag = neg ? (~src_int + 1u) : src_int;
                sp_regs[sd] = FixedToFP32(mag, neg,
                                          (state->fpscr >> 22) & 3u,
                                          &state->fpscr);
            }
            return 0;
        }
        case 0xC:
        case 0xD: {
            const bool is_signed  = (crn == 0xDu);
            const bool round_zero = (bit7 != 0u);
            const bool fz = (state->fpscr & kFpscrFzMask) != 0u;
            const double src =
                is_dp ? (fz ? FlushDenormalD(dp_regs[sm], &state->fpscr)
                            : dp_regs[sm])
                      : static_cast<double>(
                            fz ? FlushDenormalS(sp_regs[sm], &state->fpscr)
                               : sp_regs[sm]);
            const uint32_t result =
                FPToFixed32(src, is_signed, round_zero,
                            (state->fpscr >> 22) & 3u, &state->fpscr);
            const uint32_t dst_sp_idx = (crd << 1) | D;
            std::memcpy(&sp_regs[dst_sp_idx], &result, 4);
            return 0;
        }
        case 0x2:
        case 0x3:
            if (((engine_.GetSetup().arm.mvfr1 >> 24) & 0xFu) != 0u) {
                engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                    "VFP VCVTB/VCVTT half-precision conversion (opc2=0x%X, "
                    "cp%u) not implemented at guest pc=0x%08X",
                    crn, cp_num, pc);
            }
            cpu.RaiseUndefinedException(pc);
            return 1;
        default:
            if ((crn & 0xAu) == 0xAu && (is_dp ? dp_vfpv3_ : sp_vfpv3_)) {
                engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                    "VFP VCVT between floating-point and fixed-point "
                    "(opc2=0x%X, cp%u) not implemented at guest pc=0x%08X",
                    crn, cp_num, pc);
            }
            cpu.RaiseUndefinedException(pc);
            return 1;
    }
}

uint32_t CERFENGINE_CDECL ArmVfp::ExecuteCdpHelper(ArmVfp*  vfp,
                                                   uint32_t pc,
                                                   uint32_t packed) {
    return vfp->ExecuteCdp(pc, packed);
}

}
