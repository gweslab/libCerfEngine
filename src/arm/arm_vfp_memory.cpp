#include "arm_vfp_memory.h"
#include <cerfengine/calling_convention.h>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/arm/arm_mmu.h>
#include "arm_routed_access.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmVfpMemory);

uint32_t ArmVfpMemory::HandleBlockTransfer(uint32_t pc, uint32_t pc_read,
                                           uint32_t rn_idx, uint32_t vd,
                                           uint32_t imm8, uint32_t flags) {
    auto& cpu = engine_.Get<ArmCpu>();
    auto& mmu = engine_.Get<ArmMmu>();
    auto* state = cpu.State();

    const bool is_load       = (flags & kFlagL)  != 0;
    const bool writeback     = (flags & kFlagW)  != 0;
    const bool pre_decrement = (flags & kFlagP)  != 0;
    const bool is_dp         = (flags & kFlagDp) != 0;

    const uint32_t n_regs    = is_dp ? (imm8 >> 1) : imm8;
    const uint32_t bytes_per = is_dp ? 8u : 4u;

    /* DDI 0406C.c A8.8.332 VLDM (p. A8-922) and A8.8.412 VSTM (p. A8-1080),
       encoding T1/A1: "if regs == 0 || regs > 16 || (d+regs) > 32 then
       UNPREDICTABLE"; T2/A2 drops the "regs > 16" term. */
    if (n_regs == 0 || (vd + n_regs) > 32u ||
        (is_dp && n_regs > 16u)) {
        cpu.RaiseUndefinedException(pc);
        return 1;
    }

    /* DDI 0406C.c A8.8.332 VLDM Operation (p. A8-923): "address = if add then
       R[n] else R[n]-imm32" - no Align(), unlike VLDR (p. A8-925). */
    const uint32_t rn_value = rn_idx == 15u ? pc_read : state->gprs[rn_idx];
    const uint32_t imm32    = imm8 * 4u;
    uint32_t addr = pre_decrement ? (rn_value - imm32) : rn_value;

    if (mmu.AlignMultiWordOrFault(addr, !is_load)) {
        cpu.RaiseAbortDataException(pc);
        return 1;
    }

    uint8_t* vfp_base = reinterpret_cast<uint8_t*>(state->vfp_d);

    bool routed = false;
    for (uint32_t i = 0; i < n_regs; i++) {
        const uint32_t off = is_dp ? ((vd + i) * 8u) : ((vd + i) * 4u);
        uint32_t done = 0;
        if (!mmu.AccessPaged(addr, vfp_base + off, bytes_per, is_load,
                             false, &done)) {
            if (mmu.io_pending() && i == 0u && done == 0u &&
                engine_.Get<ArmInterruptChannel>().BackOutForIrq(pc)) {
                mmu.ClearIoPending();
                return 1;
            }
            if (!mmu.io_pending() ||
                !engine_.Get<ArmRoutedAccess>().WideAccess(
                    pc, addr + done, bytes_per - done,
                    vfp_base + off + done, is_load)) {
                cpu.RaiseAbortDataException(pc);
                return 1;
            }
            routed = true;
        }
        addr += bytes_per;
    }

    /* DDI 0406C.c A8.8.332 VLDM Operation, p. A8-923: "if wback then R[n] =
       if add then R[n]+imm32 else R[n]-imm32". */
    if (writeback) {
        state->gprs[rn_idx] =
            pre_decrement ? (rn_value - imm32) : (rn_value + imm32);
    }
    return (routed && engine_.Get<ArmInterruptChannel>().LeaveAfterAccess(pc + 4u)) ? 1u : 0u;
}

uint32_t CERFENGINE_CDECL ArmVfpMemory::HandleBlockTransferHelper(ArmVfpMemory* vfp,
                                                                  uint32_t pc,
                                                                  uint32_t pc_read,
                                                                  uint32_t rn_idx,
                                                                  uint32_t vd,
                                                                  uint32_t imm8,
                                                                  uint32_t flags) {
    return vfp->HandleBlockTransfer(pc, pc_read, rn_idx, vd, imm8, flags);
}

uint32_t ArmVfpMemory::HandleSingleTransfer(uint32_t pc, uint32_t pc_read,
                                            uint32_t rn_idx, uint32_t vd,
                                            int32_t signed_off,
                                            uint32_t flags) {
    auto& cpu = engine_.Get<ArmCpu>();
    auto& mmu = engine_.Get<ArmMmu>();
    auto* state = cpu.State();

    const bool is_load = (flags & kFlagL)  != 0;
    const bool is_dp   = (flags & kFlagDp) != 0;
    const uint32_t bytes = is_dp ? 8u : 4u;

    /* DDI 0406C.c A8.8.333 VLDR Operation (p. A8-925): "base = if n == 15 then
       Align(PC,4) else R[n]". */
    uint32_t addr = rn_idx == 15u ? (pc_read & ~3u) : state->gprs[rn_idx];
    addr += static_cast<uint32_t>(signed_off);

    if (mmu.AlignMultiWordOrFault(addr, !is_load)) {
        cpu.RaiseAbortDataException(pc);
        return 1;
    }

    uint8_t* vfp_base = reinterpret_cast<uint8_t*>(state->vfp_d);
    const uint32_t off = is_dp ? (vd * 8u) : (vd * 4u);
    uint32_t done = 0;
    if (!mmu.AccessPaged(addr, vfp_base + off, bytes, is_load,
                         false, &done)) {
        if (mmu.io_pending() && done == 0u &&
            engine_.Get<ArmInterruptChannel>().BackOutForIrq(pc)) {
            mmu.ClearIoPending();
            return 1;
        }
        if (!mmu.io_pending() ||
            !engine_.Get<ArmRoutedAccess>().WideAccess(
                pc, addr + done, bytes - done,
                vfp_base + off + done, is_load)) {
            cpu.RaiseAbortDataException(pc);
            return 1;
        }
        return engine_.Get<ArmInterruptChannel>().LeaveAfterAccess(pc + 4u) ? 1u : 0u;
    }
    return 0;
}

uint32_t CERFENGINE_CDECL ArmVfpMemory::HandleSingleTransferHelper(ArmVfpMemory* vfp,
                                                                   uint32_t pc,
                                                                   uint32_t pc_read,
                                                                   uint32_t rn_idx,
                                                                   uint32_t vd,
                                                                   int32_t  signed_off,
                                                                   uint32_t flags) {
    return vfp->HandleSingleTransfer(pc, pc_read, rn_idx, vd, signed_off,
                                     flags);
}

}
