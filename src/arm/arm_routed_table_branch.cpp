#include "arm_routed_table_branch.h"

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/decoded_insn.h>
#include "arm_routed_access.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmRoutedTableBranch);

bool ArmRoutedTableBranch::ShouldRegister() {
    return engine_.GetSetup().isa == Isa::kArm;
}

void ArmRoutedTableBranch::OnReady() {
    cpu_       = &engine_.Get<ArmCpu>();
    cpu_state_ = cpu_->State();
    access_    = &engine_.Get<ArmRoutedAccess>();
}

/* ARM DDI 0406C.c A8.8.236 TBB, TBH Operation (p. A8-737): "halfwords =
   UInt(MemU[R[n]+LSL(R[m],1), 2])" or "UInt(MemU[R[n]+R[m], 1])";
   "BranchWritePC(PC + 2*halfwords)". */
bool ArmRoutedTableBranch::Complete(const DecodedInsn& d) {
    const uint32_t pc      = d.guest_address + 4u;
    const uint32_t base    = d.rn == ArmGpr::kR15 ? pc : cpu_state_->gprs[d.rn];
    const uint32_t index   = cpu_state_->gprs[d.rm];
    const bool     tbh     = d.op1 != 0u;
    const uint32_t address = base + (tbh ? index << 1 : index);
    uint32_t       halfwords = 0;
    if (!access_->Load(d.guest_address, address, tbh ? 2u : 1u, &halfwords, false)) {
        cpu_->RaiseAbortDataException(d.guest_address);
        return false;
    }
    cpu_state_->gprs[ArmGpr::kR15] = pc + 2u * halfwords;
    return true;
}

}
