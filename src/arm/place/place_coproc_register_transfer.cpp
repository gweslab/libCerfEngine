#include <cstddef>
#include <cstdint>

#include <cerfengine/engine.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/arm_system_host.h>
#include "../arm_tlb_ops.h"
#include "../arm_translation_cache.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

using cerfengine::ArmCoprocAction;
using cerfengine::ArmCoprocCase;
using cerfengine::ArmCoprocDecision;
using cerfengine::ArmTlbTarget;

uint8_t* EmitFieldWriteContextSwitch(uint8_t* cursor, BlockContext* ctx,
                                     int32_t rd_disp, int32_t mmu_disp,
                                     uint32_t mask) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
    if (mask != 0xFFFFFFFFu) EmitAndRegImm32(cursor, kEax, mask);
    EmitCmpRegBaseDisp32(cursor, kEax, kMmuReg, mmu_disp);
    EmitMovBaseDisp32Reg(cursor, kMmuReg, mmu_disp, kEax);
    uint8_t* same = EmitJzLabel(cursor);
    EmitMovRegImm32(cursor, kEcx,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->TranslationCache())));
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmTranslationCache::ContextSwitchFlushHelper));
    FixupLabel(same, cursor);
    return cursor;
}

uint8_t* EmitEngineFieldRead(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx,
                             int32_t mmu_disp) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, mmu_disp);
    return EmitCoprocReadResult(cursor, d, ctx);
}

uint8_t* EmitHostRegisterRead(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx,
                              const uint32_t* reg) {
    using namespace x86;
    EmitMovRegDwordPtr(cursor, kEax, reg);
    return EmitCoprocReadResult(cursor, d, ctx);
}

uint8_t* EmitHostRegisterWriteContextSwitch(uint8_t* cursor, BlockContext* ctx,
                                            int32_t rd_disp, uint32_t* reg) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
    EmitCmpRegDwordPtr(cursor, kEax, reg);
    EmitMovDwordPtrReg(cursor, reg, kEax);
    uint8_t* same = EmitJzLabel(cursor);
    EmitMovRegImm32(cursor, kEcx,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->TranslationCache())));
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmTranslationCache::ContextSwitchFlushHelper));
    FixupLabel(same, cursor);
    return cursor;
}

uint8_t* EmitHostRegisterWriteFlushTlbs(uint8_t* cursor, int32_t rd_disp, uint32_t* reg) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
    EmitMovDwordPtrReg(cursor, reg, kEax);
    const int32_t tlbs[2] = {
        static_cast<int32_t>(offsetof(ArmMmuState, data_tlb)),
        static_cast<int32_t>(offsetof(ArmMmuState, instruction_tlb)),
    };
    for (const int32_t tlb : tlbs) {
        EmitLeaRegBaseDisp32(cursor, kEax, kMmuReg, tlb);
        EmitPushReg(cursor, kEax);
        EmitCall(cursor, reinterpret_cast<void*>(&ArmTlbFlushAll));
        EmitAddRegImm32(cursor, kEsp, 4);
    }
    return cursor;
}

uint8_t* EmitWriteValueSwitch(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx,
                              const ArmCoprocDecision& dec, int32_t rd_disp) {
    using namespace x86;
    uint8_t* to_case[cerfengine::kArmCoprocMaxCases] = {};
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
    for (uint32_t i = 0; i < dec.case_count; ++i) {
        EmitCmpRegImm32(cursor, kEax, dec.cases[i].value);
        to_case[i] = EmitJzLabel32(cursor);
    }
    cursor = EmitCoprocUnimplementedHalt(cursor, d, ctx);

    uint32_t last_body = cerfengine::kArmCoprocMaxCases;
    for (uint32_t i = 0; i < dec.case_count; ++i) {
        if (dec.cases[i].kind != ArmCoprocCase::kContinue) last_body = i;
    }
    uint8_t* to_done[cerfengine::kArmCoprocMaxCases] = {};
    for (uint32_t i = 0; i < dec.case_count; ++i) {
        if (dec.cases[i].kind == ArmCoprocCase::kContinue) continue;
        FixupLabel32(to_case[i], cursor);
        if (dec.cases[i].kind == ArmCoprocCase::kWaitForInterrupt) {
            cursor = PlaceWfi(cursor, d, ctx);
        } else {
            EmitMovRegImm32(cursor, kEcx,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dec.context)));
            EmitCall(cursor, reinterpret_cast<void*>(dec.hook));
        }
        if (i != last_body) to_done[i] = EmitJmpLabel32(cursor);
    }
    for (uint32_t i = 0; i < dec.case_count; ++i) {
        if (dec.cases[i].kind == ArmCoprocCase::kContinue) FixupLabel32(to_case[i], cursor);
        else if (to_done[i] != nullptr)                    FixupLabel32(to_done[i], cursor);
    }
    return cursor;
}

bool ValueSwitchShapeValid(const ArmCoprocDecision& dec) {
    if (dec.case_count == 0u || dec.case_count > cerfengine::kArmCoprocMaxCases) return false;
    for (uint32_t i = 0; i < dec.case_count; ++i) {
        if (dec.cases[i].kind == ArmCoprocCase::kHook && dec.hook == nullptr) return false;
    }
    return true;
}

void* TlbAllHelper(ArmTlbTarget target) {
    switch (target) {
    case ArmTlbTarget::kInstruction:
        return reinterpret_cast<void*>(&ArmTranslationCache::ItlbInvalidateAllHelper);
    case ArmTlbTarget::kData:
        return reinterpret_cast<void*>(&ArmTranslationCache::DtlbInvalidateAllHelper);
    case ArmTlbTarget::kUnified:
        return reinterpret_cast<void*>(&ArmTranslationCache::UtlbInvalidateAllHelper);
    }
    return nullptr;
}

void* TlbMvaHelper(ArmTlbTarget target) {
    switch (target) {
    case ArmTlbTarget::kInstruction:
        return reinterpret_cast<void*>(&ArmTranslationCache::ItlbInvalidateMvaHelper);
    case ArmTlbTarget::kData:
        return reinterpret_cast<void*>(&ArmTranslationCache::DtlbInvalidateMvaHelper);
    case ArmTlbTarget::kUnified:
        return reinterpret_cast<void*>(&ArmTranslationCache::UtlbInvalidateMvaHelper);
    }
    return nullptr;
}

[[noreturn]] void PanicOnShape(BlockContext* ctx, DecodedInsn* d, const char* what) {
    ctx->emit->Owner().Panic(
        "coprocessor decision %s for p%u CRn=c%u CRm=c%u opc1=%u opc2=%u %s at guest pc=0x%08X",
        what, d->cp_num, d->crn, d->crm, d->cp_opc, d->cp, d->l ? "MRC" : "MCR",
        d->guest_address);
}

uint8_t* EmitAction(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx,
                    const ArmCoprocDecision& dec) {
    using namespace x86;
    const bool    read    = d->l != 0u;
    const int32_t rd_disp = ArmGprDisp(d->rd);
    const uint32_t tc_imm =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->TranslationCache()));
    const uint32_t context_imm = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dec.context));

    switch (dec.action) {
    case ArmCoprocAction::kUndefined:
        return EmitRaiseUndAndReturn(cursor, d, ctx);

    case ArmCoprocAction::kUnimplemented:
        return EmitCoprocUnimplementedHalt(cursor, d, ctx);

    case ArmCoprocAction::kNoOp:
        return cursor;

    case ArmCoprocAction::kConstant:
        if (!read) PanicOnShape(ctx, d, "kConstant on a write");
        if (d->rd != ArmGpr::kR15) {
            EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp, dec.constant);
            return cursor;
        }
        EmitMovRegImm32(cursor, kEax, dec.constant);
        return EmitCoprocReadResult(cursor, d, ctx);

    case ArmCoprocAction::kRegister:
        if (dec.reg == nullptr) PanicOnShape(ctx, d, "kRegister without a register");
        if (read) return EmitHostRegisterRead(cursor, d, ctx, dec.reg);
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
        EmitMovDwordPtrReg(cursor, dec.reg, kEax);
        return cursor;

    case ArmCoprocAction::kRegisterContextSwitch:
        if (dec.reg == nullptr) PanicOnShape(ctx, d, "kRegisterContextSwitch without a register");
        if (read) return EmitHostRegisterRead(cursor, d, ctx, dec.reg);
        return EmitHostRegisterWriteContextSwitch(cursor, ctx, rd_disp, dec.reg);

    case ArmCoprocAction::kRegisterFlushTlbs:
        if (dec.reg == nullptr) PanicOnShape(ctx, d, "kRegisterFlushTlbs without a register");
        if (read) return EmitHostRegisterRead(cursor, d, ctx, dec.reg);
        return EmitHostRegisterWriteFlushTlbs(cursor, rd_disp, dec.reg);

    case ArmCoprocAction::kReadCallback:
        if (!read || dec.read == nullptr) PanicOnShape(ctx, d, "kReadCallback shape");
        EmitMovRegImm32(cursor, kEcx, context_imm);
        EmitCall(cursor, reinterpret_cast<void*>(dec.read));
        return EmitCoprocReadResult(cursor, d, ctx);

    case ArmCoprocAction::kReadCallbackKeyed:
        if (!read || dec.read_keyed == nullptr) PanicOnShape(ctx, d, "kReadCallbackKeyed shape");
        EmitMovRegImm32(cursor, kEcx, context_imm);
        EmitMovRegImm32(cursor, kEdx, dec.key);
        EmitCall(cursor, reinterpret_cast<void*>(dec.read_keyed));
        return EmitCoprocReadResult(cursor, d, ctx);

    case ArmCoprocAction::kWriteCallback:
        if (read || dec.write == nullptr) PanicOnShape(ctx, d, "kWriteCallback shape");
        EmitMovRegImm32(cursor, kEcx, context_imm);
        EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, rd_disp);
        EmitCall(cursor, reinterpret_cast<void*>(dec.write));
        return cursor;

    case ArmCoprocAction::kWriteCallbackKeyed:
        if (read || dec.write_keyed == nullptr) PanicOnShape(ctx, d, "kWriteCallbackKeyed shape");
        EmitMovRegImm32(cursor, kEcx, context_imm);
        EmitMovRegImm32(cursor, kEdx, dec.key);
        EmitPushBaseDisp32(cursor, kStateReg, rd_disp);
        EmitCall(cursor, reinterpret_cast<void*>(dec.write_keyed));
        return cursor;

    case ArmCoprocAction::kWriteValueSwitch:
        if (read || !ValueSwitchShapeValid(dec)) PanicOnShape(ctx, d, "kWriteValueSwitch shape");
        return EmitWriteValueSwitch(cursor, d, ctx, dec, rd_disp);

    case ArmCoprocAction::kWaitForInterrupt:
        if (read) PanicOnShape(ctx, d, "kWaitForInterrupt on a read");
        return PlaceWfi(cursor, d, ctx);

    case ArmCoprocAction::kSystemControl:
        if (read) {
            return EmitEngineFieldRead(cursor, d, ctx,
                static_cast<int32_t>(offsetof(ArmMmuState, control_register)));
        }
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, rd_disp);
        EmitCall(cursor, ctx->sctlr_write_target);
        return cursor;

    case ArmCoprocAction::kCoprocessorAccess: {
        const int32_t disp = static_cast<int32_t>(offsetof(ArmMmuState, coprocessor_access));
        if (read) return EmitEngineFieldRead(cursor, d, ctx, disp);
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
        EmitMovBaseDisp32Reg(cursor, kMmuReg, disp, kEax);
        return cursor;
    }

    case ArmCoprocAction::kContextId: {
        const int32_t disp = static_cast<int32_t>(offsetof(ArmMmuState, contextidr));
        if (read) return EmitEngineFieldRead(cursor, d, ctx, disp);
        return EmitFieldWriteContextSwitch(cursor, ctx, rd_disp, disp, 0xFFFFFFFFu);
    }

    case ArmCoprocAction::kFcsePid: {
        const int32_t disp = static_cast<int32_t>(offsetof(ArmMmuState, process_id));
        if (read) return EmitEngineFieldRead(cursor, d, ctx, disp);
        /* ARM1136 TRM (DDI 0211I) 3.3.35 Table 3-126 (p. 3-155): FCSE PID = bits[31:25],
           [24:0] "Reserved. SBZ". */
        cursor = EmitFieldWriteContextSwitch(cursor, ctx, rd_disp, disp, 0xFE000000u);
        EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, disp);
        EmitTestByteBaseDisp32Imm8(cursor, kMmuReg,
            static_cast<int32_t>(offsetof(ArmMmuState, effective_control_register)),
            0x01u);
        uint8_t* mmu_on = EmitJnzLabel(cursor);
        EmitXorRegReg(cursor, kEax, kEax);
        FixupLabel(mmu_on, cursor);
        EmitMovBaseDisp32Reg(cursor, kMmuReg,
            static_cast<int32_t>(offsetof(ArmMmuState, fcse_fold_id)), kEax);
        return cursor;
    }

    case ArmCoprocAction::kInvalidateTlbAll:
        if (read) PanicOnShape(ctx, d, "kInvalidateTlbAll on a read");
        EmitMovRegImm32(cursor, kEcx, tc_imm);
        EmitCall(cursor, TlbAllHelper(dec.tlb_target));
        return cursor;

    case ArmCoprocAction::kInvalidateTlbByMva:
        if (read) PanicOnShape(ctx, d, "kInvalidateTlbByMva on a read");
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, rd_disp);
        EmitMovRegImm32(cursor, kEdx, tc_imm);
        EmitCall(cursor, TlbMvaHelper(dec.tlb_target));
        return cursor;

    case ArmCoprocAction::kInvalidateCode:
        if (read) PanicOnShape(ctx, d, "kInvalidateCode on a read");
        EmitMovRegImm32(cursor, kEcx, tc_imm);
        EmitCall(cursor,
            reinterpret_cast<void*>(&ArmTranslationCache::InvalidateDirtyCodePagesHelper));
        return cursor;

    case ArmCoprocAction::kRegister40:
        PanicOnShape(ctx, d, "is a two-word transfer");
    }
    PanicOnShape(ctx, d, "has an unknown action");
}

}

uint8_t* PlaceCoprocRegisterTransfer(uint8_t*      cursor,
                                     DecodedInsn*  d,
                                     BlockContext* ctx) {
    bool unconditional_und = false;
    if (d->cp_num == 10u || d->cp_num == 11u) {
        cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx, false, &unconditional_und);
        if (unconditional_und) return cursor;
        return EmitVfpRegisterTransfer(cursor, d, ctx);
    }
    const ArmCoprocDecision dec =
        QueryCoprocDecision(d, ctx, cerfengine::ArmCoprocKind::kRegisterTransfer);
    cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx, dec.user_allowed,
                                             &unconditional_und);
    if (unconditional_und) return cursor;
    cursor = EmitCoprocGate(cursor, d, ctx, dec.gate_mask);
    return EmitAction(cursor, d, ctx, dec);
}

}
