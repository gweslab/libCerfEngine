#include "arm_exception_frame.h"
#include <cerfengine/calling_convention.h>

#include <cerfengine/byte_order.h>
#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_translator.h>
#include <cerfengine/arm/cpu_state.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmExceptionFrame);

void ArmExceptionFrame::OnReady() {
    cpu_        = &engine_.Get<ArmCpu>();
    cpu_state_  = cpu_->State();
    mmu_        = &engine_.Get<ArmMmu>();
    translator_ = &engine_.Get<ArmTranslator>();
}

uint32_t CERFENGINE_FASTCALL ArmExceptionFrame::RfeHelper(uint32_t           rn_value,
                                                          uint32_t           encoded,
                                                          ArmExceptionFrame* frame) {
    const bool     p_bit = (encoded & 0x80u) != 0u;
    const bool     u_bit = (encoded & 0x40u) != 0u;
    const bool     w_bit = (encoded & 0x20u) != 0u;
    const uint32_t rn    = encoded & 0x1Fu;

    /* ddi0406c B9.3.13 RFE pseudocode:
         address = if increment then R[n] else R[n]-8;
         if wordhigher then address = address+4; */
    uint32_t address = u_bit ? rn_value : (rn_value - 8u);
    if (p_bit == u_bit) {
        address += 4u;
    }

    ArmCpuState* state = frame->cpu_state_;
    cerfengine::Engine& engine = frame->engine_;

    /* ddi0406c B9.3.13 Operation: "if CurrentModeIsHyp() then UNDEFINED;
       elsif (!CurrentModeIsNotUser() ...) then UNPREDICTABLE". System mode
       is explicitly permitted. */
    const uint32_t current_mode = state->cpsr.bits.mode;
    if (current_mode == ArmMode::kUser || current_mode == ArmMode::kHyp) {
        engine.Halt(cerfengine::HaltReason::kUnpredictable,
                    "RfeHelper: executed in CPSR.M=0x%02X - UNDEFINED/"
                    "UNPREDICTABLE per ddi0406c B9.3.13", current_mode);
    }

    /* ddi0406c B3.15.5 (p. B3-1461) + Glossary "Context synchronization
       operation": returning from an exception synchronizes as its first
       step. */
    frame->mmu_->SynchronizeSctlr();

    if (frame->mmu_->AlignMultiWordOrFault(address, false)) {
        return 1u;
    }

    uint8_t* host_pc_ptr = frame->translator_->TranslateRead(address);
    if (!host_pc_ptr) {
        engine.Halt(cerfengine::HaltReason::kUnimplemented,
                    "RfeHelper: TranslateRead failed for VA 0x%08X (new_pc slot) "
                    "- RFE from non-RAM or aborted load not supported", address);
    }
    const uint32_t new_pc = le::U32(host_pc_ptr);

    uint8_t* host_cpsr_ptr = frame->translator_->TranslateRead(address + 4u);
    if (!host_cpsr_ptr) {
        engine.Halt(cerfengine::HaltReason::kUnimplemented,
                    "RfeHelper: TranslateRead failed for VA 0x%08X (new_cpsr slot)"
                    " - RFE from non-RAM or aborted load not supported", address + 4u);
    }
    const uint32_t new_cpsr = le::U32(host_cpsr_ptr);

    /* ddi0406c B9.3.13 Operation (p. B9-2001): "if wback then R[n] = ..."
       precedes "CPSRWriteByInstr(spsr_value, '1111', TRUE)". */
    if (w_bit) {
        state->gprs[rn] = u_bit ? (rn_value + 8u) : (rn_value - 8u);
    }

    /* ddi0406c B9.3.13 Operation (p. B9-2001):
         CPSRWriteByInstr(spsr_value, '1111', TRUE);
         ... else BranchWritePC(new_pc_value); */
    state->gprs[ArmGpr::kR15] =
        frame->cpu_->ReturnFromException(new_cpsr, new_pc);
    return 0u;
}

uint32_t CERFENGINE_FASTCALL ArmExceptionFrame::SrsHelper(uint32_t           encoded,
                                                          ArmExceptionFrame* frame,
                                                          uint32_t           guest_pc) {
    const bool     p_bit       = (encoded & 0x80u) != 0u;
    const bool     u_bit       = (encoded & 0x40u) != 0u;
    const bool     w_bit       = (encoded & 0x20u) != 0u;
    const uint32_t target_mode = encoded & 0x1Fu;

    ArmCpuState*        state        = frame->cpu_state_;
    cerfengine::Engine& engine       = frame->engine_;
    const uint32_t      current_mode = state->cpsr.bits.mode;

    if (current_mode == ArmMode::kUser || current_mode == ArmMode::kSystem) {
        engine.Halt(cerfengine::HaltReason::kUnpredictable,
                    "SrsHelper pc=0x%08X: executed in User/System mode "
                    "(CPSR.M=0x%X) - UNPREDICTABLE", guest_pc, current_mode);
    }
    /* ddi0406c B9.3.16: target Hyp (0x1A) is UNPREDICTABLE. */
    if (target_mode == 0x1Au) {
        engine.Halt(cerfengine::HaltReason::kUnpredictable,
                    "SrsHelper pc=0x%08X: target Hyp mode (0x1A) - UNPREDICTABLE",
                    guest_pc);
    }

    uint32_t* base_ptr = frame->cpu_->BankedSp(target_mode);

    const uint32_t base    = *base_ptr;
    uint32_t       address = u_bit ? base : (base - 8u);
    if (p_bit == u_bit) {
        address += 4u;
    }

    if (frame->mmu_->AlignMultiWordOrFault(address, true)) {
        return 1u;
    }

    uint8_t* host_lr = frame->translator_->TranslateWrite(address);
    if (!host_lr) {
        engine.Halt(cerfengine::HaltReason::kUnimplemented,
                    "SrsHelper pc=0x%08X: TranslateWrite failed for LR slot "
                    "VA 0x%08X - SRS to non-RAM or aborted store not supported",
                    guest_pc, address);
    }
    le::Put32(host_lr, state->gprs[ArmGpr::kR14]);

    uint8_t* host_spsr = frame->translator_->TranslateWrite(address + 4u);
    if (!host_spsr) {
        engine.Halt(cerfengine::HaltReason::kUnimplemented,
                    "SrsHelper pc=0x%08X: TranslateWrite failed for SPSR slot "
                    "VA 0x%08X - SRS to non-RAM or aborted store not supported",
                    guest_pc, address + 4u);
    }
    le::Put32(host_spsr, frame->cpu_->BankedSpsr(state->cpsr.bits.mode)->word);

    if (w_bit) {
        *base_ptr = u_bit ? (base + 8u) : (base - 8u);
    }
    return 0u;
}

}
