#pragma once

#include <cstdint>
#include <optional>

#include "../engine.h"
#include "../physical_memory_host.h"
#include "arm_mmu_state.h"
#include "arm_system_host.h"
#include "cpu_state.h"

namespace cerfengine {

class ArmMmu;
class PhysicalMap;

class ArmTranslator : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kArm;
    }

    uint8_t* TranslateRead     (uint32_t va);
    uint8_t* TranslateWrite    (uint32_t va);
    uint8_t* TranslateReadWrite(uint32_t va);
    uint8_t* TranslateExecute  (uint32_t va);

    uint8_t* TranslateUserRead (uint32_t va);
    uint8_t* TranslateUserWrite(uint32_t va);

    uint32_t LastExecPa() const { return last_exec_pa_; }

    bool ExecPageGlobal(uint32_t folded_va) const;

    std::optional<uint8_t*> PeekDataTlb(uint32_t va) const;
    uint8_t* PeekVaToHost(uint32_t va);
    bool     PeekVaToPa(uint32_t va, uint32_t* pa);

private:
    template <ArmMmuAccess kAccess, bool kForceUser = false>
    uint8_t* MapGuestVirtualToHost(uint32_t p);

    const ArmTlbEntry* MatchDataTlb(uint32_t va, uint32_t* folded) const;

    ArmMmu*             mmu_       = nullptr;
    ArmMmuState*        state_     = nullptr;
    ArmCpuState*        cpu_state_ = nullptr;
    PhysicalMap*        map_       = nullptr;
    void*               memory_    = nullptr;
    void*               walker_    = nullptr;

    uint32_t last_exec_pa_ = 0;
};

}
