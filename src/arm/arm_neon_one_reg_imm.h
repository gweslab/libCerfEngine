#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* One register and a modified immediate value (A7.4.6):
   VMOV / VMVN / VBIC / VORR (immediate). */
class ArmNeonOneRegImm : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kOpVmov = 0u;
    static constexpr uint32_t kOpVmvn = 1u;
    static constexpr uint32_t kOpVbic = 2u;
    static constexpr uint32_t kOpVorr = 3u;

    void HandleOneRegImm(uint32_t op_type, uint32_t d_idx,
                         uint64_t imm64, uint32_t regs);

    static void CERFENGINE_CDECL HandleOneRegImmHelper(ArmNeonOneRegImm* svc,
                                                       uint32_t op_type, uint32_t d_idx,
                                                       uint32_t imm64_lo, uint32_t imm64_hi,
                                                       uint32_t regs);
};

}
