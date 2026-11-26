#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmCpu;
struct ArmCpuState;

class ArmCpuStateService : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

protected:
    ArmCpu*      cpu_   = nullptr;
    ArmCpuState* state_ = nullptr;
};

}
