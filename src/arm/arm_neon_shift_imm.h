#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* 2-reg-shift immediate NEON ops (A7.4.4). Distinct encoding region
   (bit23=1) and shift-amount derivation from L:imm6. */
class ArmNeonShiftImm : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kSiShrS  = 0u;
    static constexpr uint32_t kSiShrU  = 1u;
    static constexpr uint32_t kSiShl   = 2u;
    static constexpr uint32_t kSiRshrS = 3u;
    static constexpr uint32_t kSiRshrU = 4u;
    /* VSRA / VRSRA: d = d + (a >> shift) [+ rounding] (A8.8.402 / A8.8.393). */
    static constexpr uint32_t kSiSraS  = 5u;
    static constexpr uint32_t kSiSraU  = 6u;
    static constexpr uint32_t kSiRsraS = 7u;
    static constexpr uint32_t kSiRsraU = 8u;
    /* VSRI / VSLI: insert shifted m into d, preserving d's bits outside
       the shift mask. No signed/unsigned distinction (A8.8.403 / A8.8.400). */
    static constexpr uint32_t kSiSri   = 9u;
    static constexpr uint32_t kSiSli   = 10u;
    /* VSHRN (A8.8.399) / VRSHRN (A8.8.390): narrowing right shift - reads
       a Q register (2*esize-bit elements), writes a D register (esize-bit
       elements). */
    static constexpr uint32_t kSiShrn  = 11u;
    static constexpr uint32_t kSiRshrn = 12u;
    /* VSHLL (A8.8.397 T1/A1): widening left shift - reads a D register
       (esize-bit elements), writes a Q register (2*esize-bit elements).
       U bit selects sign-extend vs zero-extend before the shift. */
    static constexpr uint32_t kSiShllS = 13u;
    static constexpr uint32_t kSiShllU = 14u;

    void HandleShiftImm(uint32_t op, uint32_t d_idx, uint32_t m_idx,
                        uint32_t esize, uint32_t shift_amount, uint32_t regs);

    static void CERFENGINE_CDECL HandleShiftImmHelper(ArmNeonShiftImm* svc, uint32_t op,
                                                      uint32_t d_idx, uint32_t m_idx,
                                                      uint32_t esize, uint32_t shift_amount,
                                                      uint32_t regs);

    void HandleShiftImmNarrow(uint32_t op, uint32_t d_idx, uint32_t m_idx,
                              uint32_t esize, uint32_t shift_amount);

    static void CERFENGINE_CDECL HandleShiftImmNarrowHelper(ArmNeonShiftImm* svc, uint32_t op,
                                                            uint32_t d_idx, uint32_t m_idx,
                                                            uint32_t esize, uint32_t shift_amount);

    void HandleShiftImmWiden(uint32_t op, uint32_t d_idx, uint32_t m_idx,
                             uint32_t esize, uint32_t shift_amount);

    static void CERFENGINE_CDECL HandleShiftImmWidenHelper(ArmNeonShiftImm* svc, uint32_t op,
                                                           uint32_t d_idx, uint32_t m_idx,
                                                           uint32_t esize, uint32_t shift_amount);
};

}
