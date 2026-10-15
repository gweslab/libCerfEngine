#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* Saturating 3-reg-same NEON ops (A8.8.370/382): clamp each element to the
   esize-bit signed/unsigned range and set FPSCR.QC on any saturation. */
class ArmNeonSat : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kSatAddS  = 0u;
    static constexpr uint32_t kSatAddU  = 1u;
    static constexpr uint32_t kSatSubS  = 2u;
    static constexpr uint32_t kSatSubU  = 3u;
    /* VQSHL (register, A8.8.379) + VQRSHL (A8.8.377): saturating left
       shift; right shift is truncating (VQSHL) or rounding (VQRSHL).
       No saturation on right shift - the result always fits in esize. */
    static constexpr uint32_t kSatShlS  = 4u;
    static constexpr uint32_t kSatShlU  = 5u;
    static constexpr uint32_t kSatRshlS = 6u;
    static constexpr uint32_t kSatRshlU = 7u;
    /* VQSHL/VQSHLU immediate (A8.8.380). VQSHLU is signed-in, unsigned-out:
       negative saturates to 0 (then unsigned upper clamp). */
    static constexpr uint32_t kSatShlImmS  = 8u;
    static constexpr uint32_t kSatShlImmU  = 9u;
    static constexpr uint32_t kSatShlImmSU = 10u;
    /* Saturating narrowing right shift: source is 2*esize, output esize.
       A8.8.381 VQSHRN/VQSHRUN (truncating), A8.8.378 VQRSHRN/VQRSHRUN (rounding). */
    static constexpr uint32_t kSatShrnS   = 11u;
    static constexpr uint32_t kSatShrnU   = 12u;
    static constexpr uint32_t kSatShrnSU  = 13u;
    static constexpr uint32_t kSatRShrnS  = 14u;
    static constexpr uint32_t kSatRShrnU  = 15u;
    static constexpr uint32_t kSatRShrnSU = 16u;

    void HandleSimd3SameSat(uint32_t op, uint32_t d_idx, uint32_t n_idx,
                            uint32_t m_idx, uint32_t esize, uint32_t regs);

    static void CERFENGINE_CDECL HandleSimd3SameSatHelper(ArmNeonSat* sat, uint32_t op,
                                                          uint32_t d_idx, uint32_t n_idx,
                                                          uint32_t m_idx, uint32_t esize,
                                                          uint32_t regs);

    void HandleShiftImmSat(uint32_t op, uint32_t d_idx, uint32_t m_idx,
                           uint32_t esize, uint32_t shift_amount, uint32_t regs);

    static void CERFENGINE_CDECL HandleShiftImmSatHelper(ArmNeonSat* sat, uint32_t op,
                                                         uint32_t d_idx, uint32_t m_idx,
                                                         uint32_t esize, uint32_t shift_amount,
                                                         uint32_t regs);

    void HandleShiftImmNarrowSat(uint32_t op, uint32_t d_idx, uint32_t m_idx,
                                 uint32_t esize, uint32_t shift_amount);

    static void CERFENGINE_CDECL HandleShiftImmNarrowSatHelper(ArmNeonSat* sat, uint32_t op,
                                                               uint32_t d_idx, uint32_t m_idx,
                                                               uint32_t esize, uint32_t shift_amount);

private:
    static uint64_t SatLeftShiftU(uint64_t value, int32_t shift,
                                  uint32_t esize, bool& saturated);
    static uint64_t SatLeftShiftS(int64_t value, int32_t shift,
                                  uint32_t esize, bool& saturated);
};

}
