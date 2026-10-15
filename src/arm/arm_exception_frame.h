#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

struct ArmCpuState;

class ArmCpu;
class ArmMmu;
class ArmTranslator;

class ArmExceptionFrame : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kArm;
    }

    static uint32_t CERFENGINE_FASTCALL RfeHelper(uint32_t rn_value, uint32_t encoded,
                                                  ArmExceptionFrame* frame);

    static uint32_t CERFENGINE_FASTCALL SrsHelper(uint32_t encoded,
                                                  ArmExceptionFrame* frame,
                                                  uint32_t guest_pc);

private:
    ArmCpu*        cpu_        = nullptr;
    ArmMmu*        mmu_        = nullptr;
    ArmTranslator* translator_ = nullptr;
    ArmCpuState*   cpu_state_  = nullptr;
};

}
