#include "arm_cpu_state_service.h"

#include <cerfengine/arm/arm_cpu.h>

namespace cerfengine {

bool ArmCpuStateService::ShouldRegister() {
    return engine_.GetSetup().isa == Isa::kArm;
}

void ArmCpuStateService::OnReady() {
    cpu_   = &engine_.Get<ArmCpu>();
    state_ = cpu_->State();
}

}
