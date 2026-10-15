#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmVfpMemory : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kFlagL  = 1u << 0;
    static constexpr uint32_t kFlagW  = 1u << 1;
    static constexpr uint32_t kFlagP  = 1u << 2;
    static constexpr uint32_t kFlagDp = 1u << 3;

    uint32_t HandleBlockTransfer(uint32_t pc, uint32_t pc_read,
                                 uint32_t rn_idx, uint32_t vd,
                                 uint32_t imm8, uint32_t flags);

    static uint32_t CERFENGINE_CDECL HandleBlockTransferHelper(ArmVfpMemory* vfp,
                                                               uint32_t pc,
                                                               uint32_t pc_read,
                                                               uint32_t rn_idx,
                                                               uint32_t vd,
                                                               uint32_t imm8,
                                                               uint32_t flags);

    uint32_t HandleSingleTransfer(uint32_t pc, uint32_t pc_read,
                                  uint32_t rn_idx, uint32_t vd,
                                  int32_t signed_off, uint32_t flags);

    static uint32_t CERFENGINE_CDECL HandleSingleTransferHelper(ArmVfpMemory* vfp,
                                                                uint32_t pc,
                                                                uint32_t pc_read,
                                                                uint32_t rn_idx,
                                                                uint32_t vd,
                                                                int32_t  signed_off,
                                                                uint32_t flags);
};

}
