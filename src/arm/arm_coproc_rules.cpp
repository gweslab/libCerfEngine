#include "arm_coproc_rules.h"

#include <cerfengine/engine.h>

#include "arm_emit_services.h"
#include "arm_translation_cache.h"

namespace cerfengine {

namespace {

bool ValueSwitchShapeValid(const ArmCoprocDecision& dec) {
    if (dec.case_count == 0u || dec.case_count > kArmCoprocMaxCases) return false;
    for (uint32_t i = 0; i < dec.case_count; ++i) {
        if (dec.cases[i].kind == ArmCoprocCase::kHook && dec.hook == nullptr) return false;
    }
    return true;
}

}

/* Coproc_Accepted() - ARM DDI 0406C.c A8.7.1 (p. A8-296); cp10/cp11 access
   controls - B1.11.1 (p. B1-1229). */
ArmCoprocAccessRule ArmCoprocAccessRuleFor(const ArmFeatures& features, uint32_t cp_num,
                                           bool user_allowed) {
    if (cp_num == 14u || cp_num == 15u) {
        return user_allowed ? ArmCoprocAccessRule::kAccepted
                            : ArmCoprocAccessRule::kUndefinedInUser;
    }
    /* A2.9 (p. A2-94): CP8, CP9, CP12 and CP13 are reserved; any
       coprocessor access instruction attempting to access one of these
       coprocessors is UNDEFINED. */
    if (features.arch_v7 && (cp_num == 8u || cp_num == 9u || cp_num == 12u || cp_num == 13u)) {
        return ArmCoprocAccessRule::kUndefined;
    }
    /* B1.9.2 (p. B1-1206): a coprocessor instruction that is not
       implemented raises the Undefined Instruction exception. */
    if ((cp_num == 10u || cp_num == 11u) && !features.vfp && !features.neon) {
        return ArmCoprocAccessRule::kUndefined;
    }
    return features.arch_v6 ? ArmCoprocAccessRule::kCpacr : ArmCoprocAccessRule::kAccepted;
}

const char* ArmCoprocTransferShapeError(const ArmCoprocDecision& dec, bool read) {
    switch (dec.action) {
    case ArmCoprocAction::kUndefined:
    case ArmCoprocAction::kUnimplemented:
    case ArmCoprocAction::kNoOp:
    case ArmCoprocAction::kSystemControl:
    case ArmCoprocAction::kCoprocessorAccess:
    case ArmCoprocAction::kContextId:
    case ArmCoprocAction::kFcsePid:
        return nullptr;
    case ArmCoprocAction::kConstant:
        return read ? nullptr : "kConstant on a write";
    case ArmCoprocAction::kRegister:
        return dec.reg != nullptr ? nullptr : "kRegister without a register";
    case ArmCoprocAction::kRegisterContextSwitch:
        return dec.reg != nullptr ? nullptr : "kRegisterContextSwitch without a register";
    case ArmCoprocAction::kRegisterFlushTlbs:
        return dec.reg != nullptr ? nullptr : "kRegisterFlushTlbs without a register";
    case ArmCoprocAction::kReadCallback:
        return read && dec.read != nullptr ? nullptr : "kReadCallback shape";
    case ArmCoprocAction::kReadCallbackKeyed:
        return read && dec.read_keyed != nullptr ? nullptr : "kReadCallbackKeyed shape";
    case ArmCoprocAction::kWriteCallback:
        return !read && dec.write != nullptr ? nullptr : "kWriteCallback shape";
    case ArmCoprocAction::kWriteCallbackKeyed:
        return !read && dec.write_keyed != nullptr ? nullptr : "kWriteCallbackKeyed shape";
    case ArmCoprocAction::kWriteValueSwitch:
        return !read && ValueSwitchShapeValid(dec) ? nullptr : "kWriteValueSwitch shape";
    case ArmCoprocAction::kWaitForInterrupt:
        return read ? "kWaitForInterrupt on a read" : nullptr;
    case ArmCoprocAction::kInvalidateTlbAll:
        return read ? "kInvalidateTlbAll on a read" : nullptr;
    case ArmCoprocAction::kInvalidateTlbByMva:
        return read ? "kInvalidateTlbByMva on a read" : nullptr;
    case ArmCoprocAction::kInvalidateCode:
        return read ? "kInvalidateCode on a read" : nullptr;
    case ArmCoprocAction::kRegister40:
        return "is a two-word transfer";
    }
    return kArmCoprocUnknownAction;
}

void ArmCoprocTransferShapePanic(BlockContext* ctx, const DecodedInsn* d, const char* what) {
    ctx->emit->Owner().Panic(
        "coprocessor decision %s for p%u CRn=c%u CRm=c%u opc1=%u opc2=%u %s at guest pc=0x%08X",
        what, d->cp_num, d->crn, d->crm, d->cp_opc, d->cp, d->l ? "MRC" : "MCR",
        d->guest_address);
}

void ArmCoprocDataShapePanic(BlockContext* ctx, const DecodedInsn* d, const char* kind) {
    ctx->emit->Owner().Panic(
        "coprocessor decision for %s p%u CRd=c%u at guest pc=0x%08X is neither "
        "kUndefined nor kUnimplemented", kind, d->cp_num, d->crd, d->guest_address);
}

void ArmCoprocDoubleShapePanic(BlockContext* ctx, const DecodedInsn* d) {
    ctx->emit->Owner().Panic(
        "coprocessor decision for %s p%u at guest pc=0x%08X has no two-word shape",
        d->l != 0u ? "MRRC" : "MCRR", d->cp_num, d->guest_address);
}

void* ArmTlbInvalidateAllHelperFor(ArmTlbTarget target) {
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

void* ArmTlbInvalidateMvaHelperFor(ArmTlbTarget target) {
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

}
