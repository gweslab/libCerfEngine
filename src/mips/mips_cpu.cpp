#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/calling_convention.h>

#include <cerfengine/mips/mips_system_host.h>

namespace cerfengine {

CERFENGINE_REGISTER(MipsCpu);

void MipsCpu::ResetState() {
    const MipsCpuState prior = state_;
    state_ = MipsCpuState{};
    state_.guest_cycle_counter  = prior.guest_cycle_counter;
    state_.guest_cycle_deadline = prior.guest_cycle_deadline;
    state_.guest_cycle_hi       = prior.guest_cycle_hi;
    state_.guest_cycle_folded   = prior.guest_cycle_folded;
    state_.phys_addr_mask       = engine_.GetSetup().mips.phys_addr_mask;
    engine_.GetSetup().mips_system->MipsReset(&state_);
}

void CERFENGINE_FASTCALL MipsCpu::HibernateHelper(uint32_t next_pc, MipsCpu* cpu) {
    cpu->state_.pc = next_pc;
    cpu->engine_.Host().OnPowerDownRequest();
}

}
