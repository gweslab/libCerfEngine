#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

struct MipsCpuState;

class MipsWideArithmetic : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    static void CERFENGINE_FASTCALL DmultuHelper(uint32_t rs, uint32_t rt,
                                                 MipsWideArithmetic* w);
    static void CERFENGINE_FASTCALL DmultHelper(uint32_t rs, uint32_t rt,
                                                MipsWideArithmetic* w);
    static void CERFENGINE_FASTCALL DdivHelper(uint32_t rs, uint32_t rt,
                                               MipsWideArithmetic* w);
    static void CERFENGINE_FASTCALL DdivuHelper(uint32_t rs, uint32_t rt,
                                                MipsWideArithmetic* w);

    static void CERFENGINE_FASTCALL DsllvHelper(uint32_t rd, uint32_t rt, uint32_t rs,
                                                MipsWideArithmetic* w);
    static void CERFENGINE_FASTCALL DsrlvHelper(uint32_t rd, uint32_t rt, uint32_t rs,
                                                MipsWideArithmetic* w);
    static void CERFENGINE_FASTCALL DsravHelper(uint32_t rd, uint32_t rt, uint32_t rs,
                                                MipsWideArithmetic* w);

private:
    MipsCpuState* cpu_state_ = nullptr;
};

}
