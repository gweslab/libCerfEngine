#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VMVN (register) - A8.8.354. Whole-D-register bitwise NOT. */
class ArmNeon2RegBitwiseNot : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void HandleMvn(uint32_t d_idx, uint32_t m_idx, uint32_t regs);

    static void CERFENGINE_CDECL HandleMvnHelper(ArmNeon2RegBitwiseNot* svc,
                                                 uint32_t d_idx, uint32_t m_idx,
                                                 uint32_t regs);
};

}
