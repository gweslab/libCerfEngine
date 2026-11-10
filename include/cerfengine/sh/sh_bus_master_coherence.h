#pragma once

#include <cstdint>

#include "../engine.h"

namespace cerfengine {

class PhysicalMap;
class ShTranslationCache;
struct ShCpuState;

class ShBusMasterCoherence : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }
    void OnReady() override;

    void AfterWrite(uint32_t pa, uint32_t bytes);

private:
    ShCpuState*         cpu_state_    = nullptr;
    ShTranslationCache* translations_ = nullptr;
    PhysicalMap*        map_          = nullptr;
};

}
