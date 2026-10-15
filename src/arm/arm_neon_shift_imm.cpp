#include "arm_neon_shift_imm.h"
#include <cerfengine/calling_convention.h>

#include <cstdint>
#include <cstring>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeonShiftImm);

void ArmNeonShiftImm::HandleShiftImm(uint32_t op, uint32_t d_idx, uint32_t m_idx,
                                     uint32_t esize, uint32_t shift_amount,
                                     uint32_t regs) {
    auto* state = engine_.Get<ArmCpu>().State();
    const uint32_t ebytes   = esize / 8u;
    const uint32_t elements = 8u / ebytes;
    const uint32_t sh       = 64u - esize;

    for (uint32_t r = 0; r < regs; ++r) {
        const uint8_t* dm = reinterpret_cast<const uint8_t*>(&state->vfp_d[m_idx + r]);
        const uint8_t* dd = reinterpret_cast<const uint8_t*>(&state->vfp_d[d_idx + r]);
        uint8_t res[8];
        for (uint32_t e = 0; e < elements; ++e) {
            uint64_t a = 0;
            std::memcpy(&a, dm + e * ebytes, ebytes);
            uint64_t s = 0;
            switch (op) {
                case kSiShrU: {
                    s = (shift_amount >= 64u) ? 0ull : (a >> shift_amount);
                    break;
                }
                case kSiShrS: {
                    const int64_t sa = static_cast<int64_t>(a << sh) >> sh;
                    uint32_t rs = shift_amount;
                    if (rs > 63u) rs = 63u;
                    s = static_cast<uint64_t>(sa >> rs);
                    break;
                }
                case kSiShl:
                    s = a << shift_amount;
                    break;
                case kSiRshrU: {
                    const uint32_t rs = shift_amount;
                    const uint64_t mainv = (rs >= 64u) ? 0ull : (a >> rs);
                    const uint64_t rbit  = (rs >= 65u) ? 0ull
                                                      : ((a >> (rs - 1u)) & 1ull);
                    s = mainv + rbit;
                    break;
                }
                case kSiRshrS: {
                    const int64_t sa = static_cast<int64_t>(a << sh) >> sh;
                    const uint32_t rs = shift_amount;
                    const int64_t  mainv = (rs >= 64u) ? (sa >> 63) : (sa >> rs);
                    const uint64_t rbit  = (rs >= 65u) ? (sa < 0 ? 1ull : 0ull)
                                                      : ((static_cast<uint64_t>(sa) >> (rs - 1u)) & 1ull);
                    s = static_cast<uint64_t>(mainv + static_cast<int64_t>(rbit));
                    break;
                }
                case kSiSraU: {
                    uint64_t d_val = 0;
                    std::memcpy(&d_val, dd + e * ebytes, ebytes);
                    const uint64_t shifted = (shift_amount >= 64u)
                                                 ? 0ull : (a >> shift_amount);
                    s = d_val + shifted;
                    break;
                }
                case kSiSraS: {
                    uint64_t d_val = 0;
                    std::memcpy(&d_val, dd + e * ebytes, ebytes);
                    const int64_t sa = static_cast<int64_t>(a << sh) >> sh;
                    uint32_t rs = shift_amount;
                    if (rs > 63u) rs = 63u;
                    s = d_val + static_cast<uint64_t>(sa >> rs);
                    break;
                }
                case kSiRsraU: {
                    uint64_t d_val = 0;
                    std::memcpy(&d_val, dd + e * ebytes, ebytes);
                    const uint32_t rs = shift_amount;
                    const uint64_t mainv = (rs >= 64u) ? 0ull : (a >> rs);
                    const uint64_t rbit  = (rs >= 65u) ? 0ull
                                                      : ((a >> (rs - 1u)) & 1ull);
                    s = d_val + mainv + rbit;
                    break;
                }
                case kSiRsraS: {
                    uint64_t d_val = 0;
                    std::memcpy(&d_val, dd + e * ebytes, ebytes);
                    const int64_t sa = static_cast<int64_t>(a << sh) >> sh;
                    const uint32_t rs = shift_amount;
                    const int64_t  mainv = (rs >= 64u) ? (sa >> 63)
                                                      : (sa >> rs);
                    const uint64_t rbit  =
                        (rs >= 65u) ? (sa < 0 ? 1ull : 0ull)
                                    : ((static_cast<uint64_t>(sa) >> (rs - 1u)) & 1ull);
                    const int64_t shifted = mainv + static_cast<int64_t>(rbit);
                    s = d_val + static_cast<uint64_t>(shifted);
                    break;
                }
                case kSiSri: {
                    uint64_t d_val = 0;
                    std::memcpy(&d_val, dd + e * ebytes, ebytes);
                    if (shift_amount >= esize) {
                        s = d_val;
                    } else {
                        const uint64_t shifted_m = a >> shift_amount;
                        const uint64_t keep_mask =
                            ~((1ull << (esize - shift_amount)) - 1ull);
                        s = (d_val & keep_mask) | shifted_m;
                    }
                    break;
                }
                case kSiSli: {
                    uint64_t d_val = 0;
                    std::memcpy(&d_val, dd + e * ebytes, ebytes);
                    const uint64_t shifted_m = a << shift_amount;
                    const uint64_t keep_mask = (shift_amount == 0u)
                                                   ? 0ull
                                                   : ((1ull << shift_amount) - 1ull);
                    s = (d_val & keep_mask) | shifted_m;
                    break;
                }
                default:
                    engine_.Panic("HandleShiftImm: unhandled op=%u", op);
            }
            std::memcpy(res + e * ebytes, &s, ebytes);
        }
        std::memcpy(&state->vfp_d[d_idx + r], res, 8);
    }
}

void CERFENGINE_CDECL ArmNeonShiftImm::HandleShiftImmHelper(ArmNeonShiftImm* svc, uint32_t op,
                                                            uint32_t d_idx, uint32_t m_idx,
                                                            uint32_t esize, uint32_t shift_amount,
                                                            uint32_t regs) {
    svc->HandleShiftImm(op, d_idx, m_idx, esize, shift_amount, regs);
}

void ArmNeonShiftImm::HandleShiftImmNarrow(uint32_t op, uint32_t d_idx,
                                           uint32_t m_idx, uint32_t esize,
                                           uint32_t shift_amount) {
    auto* state = engine_.Get<ArmCpu>().State();
    const uint32_t out_ebytes = esize / 8u;
    const uint32_t in_ebytes  = 2u * out_ebytes;
    const uint32_t elements   = 8u / out_ebytes;

    uint8_t src[16];
    std::memcpy(src,     &state->vfp_d[m_idx],     8);
    std::memcpy(src + 8, &state->vfp_d[m_idx + 1], 8);

    uint8_t res[8];
    for (uint32_t e = 0; e < elements; ++e) {
        uint64_t a = 0;
        std::memcpy(&a, src + e * in_ebytes, in_ebytes);
        uint64_t s = 0;
        switch (op) {
            case kSiShrn:
                s = a >> shift_amount;
                break;
            case kSiRshrn: {
                const uint32_t rs = shift_amount;
                const uint64_t mainv = a >> rs;
                const uint64_t rbit  = (a >> (rs - 1u)) & 1ull;
                s = mainv + rbit;
                break;
            }
            default:
                engine_.Panic("HandleShiftImmNarrow: unhandled op=%u", op);
        }
        std::memcpy(res + e * out_ebytes, &s, out_ebytes);
    }
    std::memcpy(&state->vfp_d[d_idx], res, 8);
}

void CERFENGINE_CDECL ArmNeonShiftImm::HandleShiftImmNarrowHelper(ArmNeonShiftImm* svc, uint32_t op,
                                                                  uint32_t d_idx, uint32_t m_idx,
                                                                  uint32_t esize, uint32_t shift_amount) {
    svc->HandleShiftImmNarrow(op, d_idx, m_idx, esize, shift_amount);
}

void ArmNeonShiftImm::HandleShiftImmWiden(uint32_t op, uint32_t d_idx,
                                          uint32_t m_idx, uint32_t esize,
                                          uint32_t shift_amount) {
    auto* state = engine_.Get<ArmCpu>().State();
    const uint32_t in_ebytes  = esize / 8u;
    const uint32_t out_ebytes = 2u * in_ebytes;
    const uint32_t elements   = 8u / in_ebytes;
    const uint32_t sh         = 64u - esize;

    const uint8_t* dm = reinterpret_cast<const uint8_t*>(&state->vfp_d[m_idx]);

    uint8_t res[16];
    for (uint32_t e = 0; e < elements; ++e) {
        uint64_t a = 0;
        std::memcpy(&a, dm + e * in_ebytes, in_ebytes);
        uint64_t s = 0;
        switch (op) {
            case kSiShllS: {
                const int64_t sa = static_cast<int64_t>(a << sh) >> sh;
                s = static_cast<uint64_t>(sa) << shift_amount;
                break;
            }
            case kSiShllU: {
                s = a << shift_amount;
                break;
            }
            default:
                engine_.Panic("HandleShiftImmWiden: unhandled op=%u", op);
        }
        std::memcpy(res + e * out_ebytes, &s, out_ebytes);
    }
    std::memcpy(&state->vfp_d[d_idx],     res,     8);
    std::memcpy(&state->vfp_d[d_idx + 1], res + 8, 8);
}

void CERFENGINE_CDECL ArmNeonShiftImm::HandleShiftImmWidenHelper(ArmNeonShiftImm* svc, uint32_t op,
                                                                 uint32_t d_idx, uint32_t m_idx,
                                                                 uint32_t esize, uint32_t shift_amount) {
    svc->HandleShiftImmWiden(op, d_idx, m_idx, esize, shift_amount);
}

}
