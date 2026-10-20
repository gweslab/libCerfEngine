#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmCpu;
class ArmRoutedAccess;
struct ArmCpuState;
struct DecodedInsn;

class ArmRoutedTableBranch : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Complete(const DecodedInsn& d);

private:
    ArmCpu*          cpu_       = nullptr;
    ArmCpuState*     cpu_state_ = nullptr;
    ArmRoutedAccess* access_    = nullptr;
};

}
