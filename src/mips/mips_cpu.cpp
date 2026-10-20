#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/calling_convention.h>

#include <cstddef>
#include <cstring>

#include <cerfengine/isa_block_space.h>
#include <cerfengine/mips/mips_system_host.h>

namespace cerfengine {

CERFENGINE_REGISTER(MipsCpu);

static_assert(offsetof(MipsCpuState, chain_exit_request) + sizeof(uint32_t) ==
                  sizeof(MipsCpuState),
              "the reset keeps the chain-exit word as the last member of the CPU state");

namespace {

/* TMPR39xx-um Figure 6-4 KUc (p.56): "0 indicates kernel mode and 1 is user mode";
   KUc is Status bit 1 (NetBSD mips cpuregs.h MIPS1_SR_KU_CUR). */
uint32_t R3000Privilege(uint32_t status) {
    return (status & 0x2u) != 0u ? kBlockUser : kBlockPrivileged;
}

/* VR4121 UM §7.3.5 (p.201): KSU "10 -> User, 01 -> Supervisor, 00 -> Kernel"; EXL or ERL
   set is Kernel mode (§6.2.7, p.165). KSU = 11 is undefined there; QEMU MIPS physaddr.c
   get_physical_address treats every mode other than User and Supervisor as Kernel. */
uint32_t R4000Privilege(uint32_t status) {
    if ((status & ((1u << MipsStatusBit::kEXL) | (1u << MipsStatusBit::kERL))) != 0u) {
        return kBlockPrivileged;
    }
    const uint32_t ksu = (status >> MipsStatusBit::kKSU) & 3u;
    if (ksu == 2u) return kBlockUser;
    if (ksu == 1u) return kBlockSupervisor;
    return kBlockPrivileged;
}

}

void MipsCpu::OnReady() {
    const bool r3000 = engine_.GetSetup().mips.isa_level == MipsIsaLevel::kMips1;
    for (uint32_t i = 0; i < 16u; ++i) {
        const uint32_t status   = i << 1;
        privilege_of_status_[i] =
            static_cast<uint8_t>(r3000 ? R3000Privilege(status) : R4000Privilege(status));
    }
}

void MipsCpu::ResetState() {
    const uint32_t counter  = state_.guest_cycle_counter;
    const uint32_t deadline = state_.guest_cycle_deadline;
    const uint32_t cycle_hi = state_.guest_cycle_hi;
    const uint32_t folded   = state_.guest_cycle_folded;
    std::memset(&state_, 0, offsetof(MipsCpuState, chain_exit_request));
    state_.guest_cycle_counter  = counter;
    state_.guest_cycle_deadline = deadline;
    state_.guest_cycle_hi       = cycle_hi;
    state_.guest_cycle_folded   = folded;
    state_.phys_addr_mask       = engine_.GetSetup().mips.phys_addr_mask;
    engine_.GetSetup().mips_system->MipsReset(&state_);
}

void CERFENGINE_FASTCALL MipsCpu::HibernateHelper(uint32_t next_pc, MipsCpu* cpu) {
    cpu->state_.pc = next_pc;
    cpu->engine_.Host().OnPowerDownRequest();
}

void CERFENGINE_FASTCALL MipsCpu::Dmtc0WidthHelper(MipsCpu* cpu, uint32_t rd) {
    cpu->engine_.Halt(HaltReason::kUnimplemented,
                      "MipsCpu: DMTC0 to cp0[%u] with a non-sign-extended 64-bit value - 64-bit "
                      "CP0 state is not modeled", rd);
}

}
