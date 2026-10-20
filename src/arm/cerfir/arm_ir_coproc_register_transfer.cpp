#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/arm/cpu_state.h>
#include "../arm_coproc_query.h"

#include "../arm_block_compiler.h"
#include "../arm_coproc_rules.h"
#include "../arm_emit_services.h"
#include "../arm_tlb_ops.h"
#include "../arm_translation_cache.h"
#include "../arm_unimplemented_halt.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::CallConv;
using cerfir::CallDesc;
using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

constexpr uint32_t kAllBits = 0xFFFFFFFFu;

void BindLabel(ArmIrContext& c, uint32_t label) {
    c.b->Label(label);
}

Ref Rt(ArmIrContext& c) { return ArmIrReadGpr(c, c.d->rd); }

Ref TranslationCache(ArmIrContext& c) { return c.b->ConstPtr(c.block->emit->TranslationCache()); }

Ref FastCall(ArmIrContext& c, const void* fn, Type result, Ref a, Ref b = kNoRef,
             Ref third = kNoRef) {
    const uint8_t count = static_cast<uint8_t>(1u + (b != kNoRef ? 1u : 0u) +
                                               (third != kNoRef ? 1u : 0u));
    const Ref     value = c.b->Call(CallDesc{fn, CallConv::kFastcall, count}, result, a, b, third);
    return value;
}

void ReadResult(ArmIrContext& c, Ref value) {
    if (c.d->rd != ArmGpr::kR15) {
        ArmIrWriteGpr(c, c.d->rd, value);
        return;
    }
    ArmIrSetNzcvFromWord(c, value);
}

void Halt(ArmIrContext& c) {
    ArmIrUnimplementedHalt(
        c, reinterpret_cast<const void*>(&ArmUnimplementedHalt::CoprocRegisterTransferHelper),
        CoprocRegisterTransferHaltInfo(c.d));
}

void ContextSwitchWrite(ArmIrContext& c, uint32_t* field, uint32_t mask) {
    cerfir::Builder& b     = *c.b;
    Ref              value = Rt(c);
    if (mask != kAllBits) value = b.Op2(Op::kAnd, value, b.Const32(mask));
    const uint32_t same = b.NewLabel();
    b.Branch(b.HostReplace(field, value), same, false);
    FastCall(c, reinterpret_cast<const void*>(&ArmTranslationCache::ContextSwitchFlushHelper),
             Type::kVoid, TranslationCache(c));
    BindLabel(c, same);
}

void FcsePidWrite(ArmIrContext& c) {
    cerfir::Builder& b   = *c.b;
    ArmMmuState*     mmu = c.block->emit->Mmu()->State();
    ContextSwitchWrite(c, &mmu->process_id, kArmFcsePidMask);
    const Ref pid = b.HostLoad(&mmu->process_id, 4u, false);
    const Ref on  = b.HostTest(&mmu->effective_control_register, 1u);
    b.HostStore(&mmu->fcse_fold_id, b.Op3(Op::kSelect, on, pid, b.Const32(0u)), 4u);
}

void FlushTlbs(ArmIrContext& c) {
    FastCall(c, reinterpret_cast<const void*>(&ArmMmu::InvalidateAllTlbsHelper), Type::kVoid,
             c.b->ConstPtr(c.block->emit->Mmu()));
}

void ValueSwitch(ArmIrContext& c, const ArmCoprocDecision& dec) {
    cerfir::Builder& b     = *c.b;
    const Ref        value = Rt(c);
    b.Share(value, dec.case_count);
    const uint32_t   done  = b.NewLabel();
    uint32_t         target[kArmCoprocMaxCases] = {};
    uint32_t         last_body                  = kArmCoprocMaxCases;
    for (uint32_t i = 0; i < dec.case_count; ++i) {
        const bool proceed = dec.cases[i].kind == ArmCoprocCase::kContinue;
        target[i]          = proceed ? done : b.NewLabel();
        if (!proceed) last_body = i;
        b.Branch(b.Op2(Op::kCmpEq, value, b.Const32(dec.cases[i].value)), target[i], true);
    }
    Halt(c);
    for (uint32_t i = 0; i < dec.case_count; ++i) {
        if (dec.cases[i].kind == ArmCoprocCase::kContinue) continue;
        BindLabel(c, target[i]);
        if (dec.cases[i].kind == ArmCoprocCase::kWaitForInterrupt) {
            ArmIrWfi(c);
        } else {
            FastCall(c, reinterpret_cast<const void*>(dec.hook), Type::kVoid,
                     b.ConstPtr(dec.context));
        }
        if (i != last_body) b.Branch(b.Const1(true), done, true);
    }
    BindLabel(c, done);
}

void Action(ArmIrContext& c, const ArmCoprocDecision& dec) {
    cerfir::Builder& b    = *c.b;
    const bool       read = c.d->l != 0u;
    ArmMmuState*     mmu  = c.block->emit->Mmu()->State();
    switch (dec.action) {
    case ArmCoprocAction::kUndefined:
        ArmIrUndefined(c);
        return;
    case ArmCoprocAction::kUnimplemented:
        Halt(c);
        return;
    case ArmCoprocAction::kNoOp:
        return;
    case ArmCoprocAction::kConstant:
        ReadResult(c, b.Const32(dec.constant));
        return;
    case ArmCoprocAction::kRegister:
        if (read) {
            ReadResult(c, b.HostLoad(dec.reg, 4u, false));
        } else {
            b.HostStore(dec.reg, Rt(c), 4u);
        }
        return;
    case ArmCoprocAction::kRegisterContextSwitch:
        if (read) {
            ReadResult(c, b.HostLoad(dec.reg, 4u, false));
        } else {
            ContextSwitchWrite(c, dec.reg, kAllBits);
        }
        return;
    case ArmCoprocAction::kRegisterFlushTlbs:
        if (read) {
            ReadResult(c, b.HostLoad(dec.reg, 4u, false));
            return;
        }
        b.HostStore(dec.reg, Rt(c), 4u);
        FlushTlbs(c);
        return;
    case ArmCoprocAction::kReadCallback:
        ReadResult(c, FastCall(c, reinterpret_cast<const void*>(dec.read), Type::kI32,
                               b.ConstPtr(dec.context)));
        return;
    case ArmCoprocAction::kReadCallbackKeyed:
        ReadResult(c, FastCall(c, reinterpret_cast<const void*>(dec.read_keyed), Type::kI32,
                               b.ConstPtr(dec.context), b.Const32(dec.key)));
        return;
    case ArmCoprocAction::kWriteCallback:
        FastCall(c, reinterpret_cast<const void*>(dec.write), Type::kVoid, b.ConstPtr(dec.context),
                 Rt(c));
        return;
    case ArmCoprocAction::kWriteCallbackKeyed:
        FastCall(c, reinterpret_cast<const void*>(dec.write_keyed), Type::kVoid,
                 b.ConstPtr(dec.context), b.Const32(dec.key), Rt(c));
        return;
    case ArmCoprocAction::kWriteValueSwitch:
        ValueSwitch(c, dec);
        return;
    case ArmCoprocAction::kWaitForInterrupt:
        ArmIrWfi(c);
        return;
    case ArmCoprocAction::kSystemControl:
        if (read) {
            ReadResult(c, b.HostLoad(&mmu->control_register, 4u, false));
        } else {
            FastCall(c, reinterpret_cast<const void*>(&ArmBlockCompiler::SctlrWriteHelper),
                     Type::kVoid, Rt(c), b.ConstPtr(c.compiler));
        }
        return;
    case ArmCoprocAction::kCoprocessorAccess:
        if (read) {
            ReadResult(c, b.HostLoad(&mmu->coprocessor_access, 4u, false));
        } else {
            b.HostStore(&mmu->coprocessor_access, Rt(c), 4u);
        }
        return;
    case ArmCoprocAction::kContextId:
        if (read) {
            ReadResult(c, b.HostLoad(&mmu->contextidr, 4u, false));
        } else {
            ContextSwitchWrite(c, &mmu->contextidr, kAllBits);
        }
        return;
    case ArmCoprocAction::kFcsePid:
        if (read) {
            ReadResult(c, b.HostLoad(&mmu->process_id, 4u, false));
        } else {
            FcsePidWrite(c);
        }
        return;
    case ArmCoprocAction::kInvalidateTlbAll:
        FastCall(c, ArmTlbInvalidateAllHelperFor(dec.tlb_target), Type::kVoid, TranslationCache(c));
        return;
    case ArmCoprocAction::kInvalidateTlbByMva:
        FastCall(c, ArmTlbInvalidateMvaHelperFor(dec.tlb_target), Type::kVoid, Rt(c),
                 TranslationCache(c));
        return;
    case ArmCoprocAction::kInvalidateCode:
        FastCall(c,
                 reinterpret_cast<const void*>(&ArmTranslationCache::InvalidateDirtyCodePagesHelper),
                 Type::kVoid, TranslationCache(c));
        return;
    case ArmCoprocAction::kRegister40:
        break;
    }
    ArmCoprocTransferShapePanic(c.block, c.d, kArmCoprocUnknownAction);
}

}

void ArmIrCoprocRegisterTransfer(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    if (d.cp_num == 10u || d.cp_num == 11u) {
        ArmIrVfpRegisterTransfer(c);
        return;
    }
    const ArmCoprocDecision dec = QueryCoprocDecision(&d, c.block, ArmCoprocKind::kRegisterTransfer);
    if (ArmIrCoprocessorPermissionCheck(c, dec.user_allowed)) return;
    ArmIrCoprocGate(c, dec.gate_mask);
    if (const char* what = ArmCoprocTransferShapeError(dec, d.l != 0u)) {
        ArmCoprocTransferShapePanic(c.block, &d, what);
    }
    Action(c, dec);
}

}
