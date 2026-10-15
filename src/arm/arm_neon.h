#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmMmu;

class ArmNeon : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    uint32_t HandleVdup(uint32_t pc, uint32_t d_idx, uint32_t rt_idx,
                        uint32_t esize, uint32_t regs);

    static uint32_t CERFENGINE_CDECL HandleVdupHelper(ArmNeon* neon, uint32_t pc,
                                                      uint32_t d_idx, uint32_t rt_idx,
                                                      uint32_t esize, uint32_t regs);

    static constexpr uint32_t kLsLoad = 1u << 0;

    static uint32_t CERFENGINE_CDECL LoadAllLanesHelper(ArmNeon* neon, uint32_t pc,
                                              uint32_t instruction);

    /* VLD1 / VST1 (multiple single elements),
       DDI0406C A8.8.320 / A8.8.404. */
    uint32_t HandleLoadStoreMultiple(uint32_t pc, uint32_t d_idx, uint32_t rn_idx,
                                     uint32_t rm_idx, uint32_t flags);

    static uint32_t CERFENGINE_CDECL HandleLoadStoreMultipleHelper(ArmNeon* neon, uint32_t pc,
                                                                   uint32_t d_idx, uint32_t rn_idx,
                                                                   uint32_t rm_idx, uint32_t flags);

    /* VLD2/3/4 + VST2/3/4 (multiple N-element structures, de-interleaved),
       DDI0406C A8.8.323/326/329 / A8.8.406/408/410. */
    uint32_t HandleLoadStoreInterleaved(uint32_t pc, uint32_t d_idx, uint32_t rn_idx,
                                        uint32_t rm_idx, uint32_t flags);

    static uint32_t CERFENGINE_CDECL HandleLoadStoreInterleavedHelper(ArmNeon* neon, uint32_t pc,
                                                                      uint32_t d_idx, uint32_t rn_idx,
                                                                      uint32_t rm_idx, uint32_t flags);

    uint32_t HandleLoadStoreSingleLane(uint32_t pc, uint32_t d_idx, uint32_t rn_idx,
                                       uint32_t rm_idx, uint32_t flags);

    static uint32_t CERFENGINE_CDECL HandleLoadStoreSingleLaneHelper(ArmNeon* neon, uint32_t pc,
                                                                     uint32_t d_idx, uint32_t rn_idx,
                                                                     uint32_t rm_idx, uint32_t flags);

    static constexpr uint32_t kAccMla  = 0u;
    static constexpr uint32_t kAccMls  = 1u;
    /* A8.8.277 VABA (S/U): d = d + |n-m| (signed/unsigned absdiff). */
    static constexpr uint32_t kAccAbaS = 2u;
    static constexpr uint32_t kAccAbaU = 3u;
    void HandleSimd3SameAcc(uint32_t op, uint32_t d_idx, uint32_t n_idx,
                            uint32_t m_idx, uint32_t esize, uint32_t regs);
    static void CERFENGINE_CDECL HandleSimd3SameAccHelper(ArmNeon* neon, uint32_t op,
                                                          uint32_t d_idx, uint32_t n_idx,
                                                          uint32_t m_idx, uint32_t esize,
                                                          uint32_t regs);

    /* Pairwise reduction WITHIN each input register - low half of Dd from
       Dn pairs, high half from Dm pairs. Doubleword only (Q=0).
       A8.8.362 VPADD, A8.8.365 VPMAX/VPMIN. */
    static constexpr uint32_t kPwAdd  = 0u;
    static constexpr uint32_t kPwMaxS = 1u;
    static constexpr uint32_t kPwMaxU = 2u;
    static constexpr uint32_t kPwMinS = 3u;
    static constexpr uint32_t kPwMinU = 4u;
    void HandleSimd3SamePairwise(uint32_t op, uint32_t d_idx, uint32_t n_idx,
                                 uint32_t m_idx, uint32_t esize);
    static void CERFENGINE_CDECL HandleSimd3SamePairwiseHelper(ArmNeon* neon, uint32_t op,
                                                               uint32_t d_idx, uint32_t n_idx,
                                                               uint32_t m_idx, uint32_t esize);

private:
    /* DDI0406C Table A3-1, p. A3-108: with :<align> the check is as specified
       and faults under both SCTLR.A 0 and 1; with standard alignment the check
       is Element size and faults only under SCTLR.A 1. */
    bool AlignmentFaults(ArmMmu& mmu, uint32_t base, uint32_t alignment,
                         uint32_t ebytes, bool is_load);
};

}
