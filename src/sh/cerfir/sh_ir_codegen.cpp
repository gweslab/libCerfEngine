#include "sh_ir_codegen.h"

#include <cstddef>
#include <iterator>
#include <string>

#include <cerfengine/engine_host.h>
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include <cerfengine/sh/sh_mmu.h>
#include "../../cerfir/cerfir_builder.h"
#include "../../cerfir/cerfir_verify.h"
#include "../../host_platform.h"
#include "../sh_memory_access.h"
#include "sh_ir_fns.h"

namespace cerfengine {

CERFENGINE_REGISTER(ShIrCodegen);

namespace {

using cerfir::kNoRef;
using cerfir::Type;

constexpr uint32_t kNoLdtlbShadow             = ~0u;
constexpr uint32_t kExpevtReservedInstruction = 0x180u;
constexpr uint32_t kExpevtIllegalSlot         = 0x1A0u;

void TranslateNop(ShIrContext&) {}

using Translator = void (*)(ShIrContext&);

constexpr Translator kTranslators[] = {
    &TranslateNop,
    &ShIrTranslateMovPcRel,
    &ShIrTranslateJump,
    &ShIrTranslateMov,
    &ShIrTranslateMovImm,
    &ShIrTranslateMovStore,
    &ShIrTranslateMovLoad,
    &ShIrTranslateMovStorePreDec,
    &ShIrTranslateMovLoadPostInc,
    &ShIrTranslateMovStoreIndexed,
    &ShIrTranslateMovLoadIndexed,
    &ShIrTranslateCmp,
    &ShIrTranslateConditionalBranch,
    &ShIrTranslateConditionalBranch,
    &ShIrTranslateBranchAlways,
    &ShIrTranslateSystemLoad,
    &ShIrTranslateSystemLoadPostInc,
    &ShIrTranslateSystemStore,
    &ShIrTranslateSystemStorePreDec,
    &ShIrTranslateControlLoad,
    &ShIrTranslateControlLoadPostInc,
    &ShIrTranslateControlStore,
    &ShIrTranslateControlStorePreDec,
    &ShIrTranslateLogic,
    &ShIrTranslateLogicImm,
    &ShIrTranslateLogicGbr,
    &ShIrTranslateNot,
    &ShIrTranslateTas,
    &ShIrTranslateArith,
    &ShIrTranslateAddImm,
    &ShIrTranslateShift,
    &ShIrTranslateMovStoreDisp,
    &ShIrTranslateMovLoadDisp,
    &ShIrTranslateMovStoreGbr,
    &ShIrTranslateMovLoadGbr,
    &ShIrTranslateMova,
    &ShIrTranslateMovt,
    &ShIrTranslateSwap,
    &ShIrTranslateXtrct,
    &ShIrTranslateMultiply,
    &ShIrTranslateClrmac,
    &ShIrTranslateLdtlb,
    &ShIrTranslateRte,
    &TranslateNop,
    &ShIrTranslateClearSrBits,
    &ShIrTranslateSetSrBits,
    &ShIrTranslateSleep,
};

static_assert(std::size(kTranslators) + 1u == static_cast<size_t>(ShInsnKind::kCount),
              "every implemented SH instruction kind has a translator");

}

void ShIrCodegen::OnReady() {
    host_                        = &engine_.Host();
    cpu_                         = &engine_.Get<ShCpu>();
    mmu_                         = &engine_.Get<ShMmu>();
    cpu_state_                   = cpu_->State();
    verify_                      = engine_.GetSetup().verify_ir;
    model_.cycle_slot            = static_cast<int32_t>(ShStateDisp(offsetof(ShCpuState, guest_cycle_counter)));
    cerfir::ExitModel& e         = model_.exits;
    e.chain                      = true;
    e.chain_exit_slot            = static_cast<int32_t>(ShStateDisp(offsetof(ShCpuState, chain_exit_request)));
    e.cycle_slot                 = model_.cycle_slot;
    e.deadline_slot              = static_cast<int32_t>(ShStateDisp(offsetof(ShCpuState, guest_cycle_deadline)));
    model_.memory                = cerfir::MemoryModelKind::kHelper;
    cerfir::HelperMemoryModel& h = model_.helper;
    static_assert(kShMemoryModes <= cerfir::kMaxPrivileges);
    for (uint32_t m = 0; m < kShMemoryModes; ++m) {
        const ShMemoryHelpers& p = ShMemoryAccess::HelpersFor(static_cast<ShMemoryMode>(m));
        for (uint32_t w = 0; w < 3u; ++w) {
            h.load[m][w]  = reinterpret_cast<const void*>(p.load[w]);
            h.store[m][w] = reinterpret_cast<const void*>(p.store[w]);
        }
    }
    h.context     = &engine_.Get<ShMemoryAccess>();
    h.store_pc    = true;
    target_.model = &model_;
    block_.SetPcSlot(static_cast<int32_t>(ShPcDisp()));
}

size_t ShIrCodegen::Generate(const ShDecodedInsn* insns, uint32_t count, const cerfir::BlockLink* link,
                             uint8_t* code, uint8_t* code_end) {
    target_.link = link;
    block_.Reset();
    lowering_.Begin(block_, code, code_end, target_);
    TranslateBlock(insns, count);
    if (verify_) {
        std::string error;
        if (!cerfir::Verify(block_, &error)) {
            engine_.Panic("%s (block at guest PC 0x%08X)", error.c_str(), insns[0].guest_address);
        }
    }
    return lowering_.Finish();
}

void CERFENGINE_CDECL ShIrCodegen::TraceDispatchPcHelper(ShIrCodegen* codegen, uint32_t guest_pc) {
    codegen->host_->OnShPcTrace(guest_pc, codegen->cpu_state_);
}

void ShIrCodegen::EmitPcHelperCall(cerfir::Builder& b, PcHelper helper, uint32_t guest_pc) {
    cerfir::CallSpec spec{reinterpret_cast<const void*>(helper), cerfir::CallConv::kCdecl, Type::kVoid};
    spec.store_pc = true;
    spec.pc       = guest_pc;
    b.CallConst(spec, 2u, cerfir::CallHostAddress(this), guest_pc, 0u, 0u);
}

void ShIrCodegen::RaiseIllegalInstruction(uint32_t expevt, uint32_t exception_pc) {
    cpu_->RaiseException(expevt, kShVectorGeneral, exception_pc);
    platform::RaiseUnwind(platform::kGuestUnwindCode);
}

template <uint32_t kExpevt>
void CERFENGINE_CDECL ShIrCodegen::IllegalInstructionHelper(ShIrCodegen* codegen, uint32_t exception_pc) {
    codegen->RaiseIllegalInstruction(kExpevt, exception_pc);
}

template <uint32_t kExpevt>
void CERFENGINE_CDECL ShIrCodegen::DspModeCheckHelper(ShIrCodegen* codegen, uint32_t exception_pc) {
    if ((codegen->cpu_state_->sr & kShSrDsp) != 0u) return;
    codegen->RaiseIllegalInstruction(kExpevt, exception_pc);
}

template <uint32_t kExpevt>
void CERFENGINE_CDECL ShIrCodegen::PrivilegedModeCheckHelper(ShIrCodegen* codegen, uint32_t exception_pc) {
    if ((codegen->cpu_state_->sr & kShSrMd) != 0u) return;
    codegen->RaiseIllegalInstruction(kExpevt, exception_pc);
}

void ShIrCodegen::TranslateBlock(const ShDecodedInsn* insns, uint32_t count) {
    cerfir::Builder b(block_, lowering_, verify_);
    ShIrContext     c{&b, nullptr, cpu_, mmu_, false, kNoRef, false, 0u, kNoRef, false, false};
    const bool      traced      = host_->HasPcTraceIn(insns[0].guest_address, insns[count - 1u].guest_address);
    const bool      user        = target_.link->privilege == static_cast<uint32_t>(ShMemoryMode::kUser);
    uint32_t        shadow_last = kNoLdtlbShadow;
    auto            begin       = [&](const ShDecodedInsn& d) {
        c.d                          = &d;
        const uint32_t index         = static_cast<uint32_t>(&d - insns);
        c.in_ldtlb_shadow            = shadow_last != kNoLdtlbShadow && index <= shadow_last;
        const bool     slot          = c.in_delay_slot;
        const bool     rte_slot      = slot && insns[index - 1u].kind == ShInsnKind::kRte;
        const uint32_t exception_pc  = slot ? insns[index - 1u].guest_address : d.guest_address;
        if (traced && host_->HasPcTrace(d.guest_address)) {
            EmitPcHelperCall(b, &ShIrCodegen::TraceDispatchPcHelper, d.guest_address);
        }
        b.CycleAdd(d.cycles);
        if ((slot && d.slot_illegal != 0u) || d.kind == ShInsnKind::kReserved) {
            EmitPcHelperCall(b,
                             slot ? &ShIrCodegen::IllegalInstructionHelper<kExpevtIllegalSlot>
                                  : &ShIrCodegen::IllegalInstructionHelper<kExpevtReservedInstruction>,
                             exception_pc);
            return false;
        }
        if (d.privileged != 0u && (user || rte_slot)) {
            EmitPcHelperCall(b,
                             slot ? &ShIrCodegen::PrivilegedModeCheckHelper<kExpevtIllegalSlot>
                                  : &ShIrCodegen::PrivilegedModeCheckHelper<kExpevtReservedInstruction>,
                             exception_pc);
        }
        if (d.dsp_mode != 0u) {
            EmitPcHelperCall(b,
                             slot ? &ShIrCodegen::DspModeCheckHelper<kExpevtIllegalSlot>
                                  : &ShIrCodegen::DspModeCheckHelper<kExpevtReservedInstruction>,
                             exception_pc);
        }
        return true;
    };
    auto translate = [&](const ShDecodedInsn& d) {
        kTranslators[static_cast<size_t>(d.kind) - 1u](c);
        const uint32_t index = static_cast<uint32_t>(&d - insns);
        if (d.kind == ShInsnKind::kLdtlb) {
            shadow_last = index + kLdtlbShadowInsns;
        } else if (index == shadow_last) {
            b.SetSlotConst(ShStateDisp(offsetof(ShCpuState, ldtlb_shadow)), 0u);
        }
    };
    for (uint32_t i = 0; i < count; ++i) {
        const ShDecodedInsn& d = insns[i];
        if (!begin(d)) {
            b.ExitDispatcher();
            return;
        }
        if (d.delayed_branch != 0u && d.kind != ShInsnKind::kBranchDelayed && insns[i + 1u].slot_illegal != 0u) {
            if (d.kind == ShInsnKind::kRte) b.SetSlotConst(ShStateDisp(offsetof(ShCpuState, rte_slot)), 1u);
            ShIrLinkProcedure(c);
            c.in_delay_slot = true;
            begin(insns[i + 1u]);
            b.ExitDispatcher();
            return;
        }
        translate(d);
        if (d.kind == ShInsnKind::kBranch) {
            const uint32_t taken = b.NewLabel();
            b.Branch(c.t_clear, taken, c.taken_if_t_clear);
            b.ExitDirect(d.guest_address + 2u, 1u, true, true);
            b.Label(taken);
            b.CycleAdd(2u);
            b.ExitDirect(d.disp, 0u, true, true);
            return;
        }
        if (d.kind == ShInsnKind::kBranchDelayed) {
            const ShDecodedInsn& slot  = insns[i + 1u];
            const uint32_t       taken = b.NewLabel();
            b.Branch(c.t_clear, taken, c.taken_if_t_clear);
            if (slot.slot_illegal != 0u) {
                b.ExitDirect(slot.guest_address, 1u, true, true);
            } else {
                begin(slot);
                translate(slot);
                b.ExitDirect(slot.guest_address + 2u, 1u, true, true);
            }
            b.Label(taken);
            b.CycleAdd(1u);
            c.in_delay_slot = true;
            if (!begin(slot)) {
                b.ExitDispatcher();
                return;
            }
            c.branch_target = b.Const32(d.disp);
            translate(slot);
            b.ExitDirect(d.disp, 0u, true, true);
            return;
        }
        if (c.in_delay_slot) {
            if (insns[i - 1u].kind == ShInsnKind::kRte) {
                b.SetSlotConst(ShStateDisp(offsetof(ShCpuState, rte_slot)), 0u);
                b.SetSlot(ShPcDisp(), c.branch_target);
                b.ExitDispatcher();
            } else if (c.branch_target_static) {
                b.ExitDirect(c.static_target, 0u, true, true);
            } else {
                b.SetSlot(ShPcDisp(), c.branch_target);
                b.ExitIndirect();
            }
            return;
        }
        if (d.ends_block != 0u) {
            b.SetSlotConst(ShPcDisp(), d.guest_address + 2u);
            b.ExitDispatcher();
            return;
        }
        if (d.delayed_branch != 0u) c.in_delay_slot = true;
    }
    const ShDecodedInsn& last = insns[count - 1u];
    b.ExitDirect(last.guest_address + 2u, 0u, true, true);
}

}
