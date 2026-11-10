#include <cerfengine/sh/sh_cpu.h>

#include <cerfengine/sh/sh_mmu.h>

#include <atomic>
#include <utility>

namespace cerfengine {

CERFENGINE_REGISTER(ShCpu);

namespace {

constexpr uint32_t kResetSr              = 0x700000F0u;
constexpr uint32_t kResetPc              = 0xA0000000u;
constexpr uint32_t kPowerOnResetExpevt   = 0x000u;

constexpr bool Bank1Active(uint32_t sr) { return (sr & kShSrMd) != 0u && (sr & kShSrRb) != 0u; }

}

void ShCpu::OnReady() {
    sr_writable_ = engine_.GetSetup().sh.dsp ? kShSrWritableDsp : kShSrWritableSh3;
}

void ShCpu::ResetState() {
    state_.sr     = kResetSr;
    state_.vbr    = 0u;
    state_.pc     = kResetPc;
    state_.dsr    = 0u;
    state_.expevt       = kPowerOnResetExpevt;
    state_.ldtlb_shadow = 0u;
    state_.rte_slot     = 0u;
    state_.sleeping     = kShSleepNone;
    state_.deep_sleep   = 0u;
    state_.mmio_access  = 0u;
    std::atomic_ref<uint32_t>(state_.chain_exit_request).fetch_and(~kShChainExitSleep, std::memory_order_acq_rel);
    engine_.Get<ShMmu>().PowerOnReset();
}

void ShCpu::WriteSr(uint32_t value) {
    const uint32_t sr = value & sr_writable_;
    if ((sr & kShSrRc) != 0u) {
        engine_.Halt(HaltReason::kUnimplemented,
                     "ShCpu: SR write 0x%08X at PC 0x%08X sets the repeat counter, which is not modeled", value,
                     state_.pc);
    }
    if (Bank1Active(sr) != Bank1Active(state_.sr)) {
        for (uint32_t i = 0; i < kShNumBank; ++i) std::swap(state_.gpr[i], state_.bank[i]);
    }
    state_.sr = sr;
}

void ShCpu::EnterHandler(uint32_t return_pc, uint32_t vector_offset) {
    state_.ssr = state_.sr;
    state_.spc = return_pc;
    WriteSr(state_.sr | kShSrMd | kShSrRb | kShSrBl);
    state_.pc = state_.vbr + vector_offset;
}

void ShCpu::RaiseException(uint32_t expevt, uint32_t vector_offset, uint32_t exception_pc) {
    if ((state_.sr & kShSrBl) != 0u) {
        engine_.Halt(HaltReason::kUnimplemented,
                     "ShCpu: exception 0x%03X at PC 0x%08X with SR.BL set resets the CPU, which is not modeled", expevt,
                     exception_pc);
    }
    if (state_.rte_slot != 0u) {
        engine_.Halt(HaltReason::kUnimplemented, "ShCpu: exception 0x%03X in the delay slot of RTE at PC 0x%08X",
                     expevt, exception_pc);
    }
    state_.expevt = expevt;
    EnterHandler(exception_pc, vector_offset);
}

void ShCpu::EnterInterrupt(uint32_t intevt) {
    state_.intevt = intevt;
    EnterHandler(state_.pc, kShVectorInterrupt);
}

void CERFENGINE_CDECL ShCpu::WriteSrHelper(ShCpu* cpu, uint32_t value) {
    cpu->WriteSr(value);
}

void CERFENGINE_CDECL ShCpu::EnterSleepHelper(ShCpu* cpu) {
    cpu->state_.sleeping = kShSleepRequested;
    std::atomic_ref<uint32_t>(cpu->state_.chain_exit_request).fetch_or(kShChainExitSleep, std::memory_order_acq_rel);
}

}
