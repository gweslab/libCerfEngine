#include <array>
#include <cstddef>

#include "../plan/plan_fns.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

constexpr std::array<ArmIrTranslateFn, kArmInsnKindCount> BuildTranslateTable() {
    std::array<ArmIrTranslateFn, kArmInsnKindCount> t{};
    auto set = [&t](ArmInsnKind kind, ArmIrTranslateFn fn) {
        t[static_cast<size_t>(kind)] = fn;
    };
    set(ArmInsnKind::kUndefined,                &ArmIrUndefined);
    set(ArmInsnKind::kNop,                      &ArmIrNop);
    set(ArmInsnKind::kSingleDataTransfer,       &ArmIrSingleDataTransfer);
    set(ArmInsnKind::kMultiply,                 &ArmIrMultiply);
    set(ArmInsnKind::kLoadStoreExtension,       &ArmIrLoadStoreExtension);
    set(ArmInsnKind::kBlockDataTransfer,        &ArmIrBlockDataTransfer);
    set(ArmInsnKind::kClz,                      &ArmIrClz);
    set(ArmInsnKind::kRev,                      &ArmIrRev);
    set(ArmInsnKind::kRev16,                    &ArmIrRev16);
    set(ArmInsnKind::kRevsh,                    &ArmIrRevsh);
    set(ArmInsnKind::kRbit,                     &ArmIrRbit);
    set(ArmInsnKind::kSxtb,                     &ArmIrSxtb);
    set(ArmInsnKind::kSxth,                     &ArmIrSxth);
    set(ArmInsnKind::kUxtb,                     &ArmIrUxtb);
    set(ArmInsnKind::kUxth,                     &ArmIrUxth);
    set(ArmInsnKind::kBfc,                      &ArmIrBfc);
    set(ArmInsnKind::kBfi,                      &ArmIrBfi);
    set(ArmInsnKind::kUbfx,                     &ArmIrUbfx);
    set(ArmInsnKind::kSbfx,                     &ArmIrSbfx);
    set(ArmInsnKind::kSaturatingArith,          &ArmIrSaturatingArith);
    set(ArmInsnKind::kHalfwordMultiply,         &ArmIrHalfwordMultiply);
    set(ArmInsnKind::kDualMultiply,             &ArmIrDualMultiply);
    set(ArmInsnKind::kMostSignificantMultiply,  &ArmIrMostSignificantMultiply);
    set(ArmInsnKind::kSsat,                     &ArmIrSsat);
    set(ArmInsnKind::kRfe,                      &ArmIrRfe);
    set(ArmInsnKind::kSrs,                      &ArmIrSrs);
    set(ArmInsnKind::kWfi,                      &ArmIrWfi);
    set(ArmInsnKind::kArmUnimplemented,         &ArmIrArmUnimplemented);
    set(ArmInsnKind::kLdrex,                    &ArmIrLdrex);
    set(ArmInsnKind::kStrex,                    &ArmIrStrex);
    set(ArmInsnKind::kMrsOrMsr,                 &ArmIrMrsOrMsr);
    set(ArmInsnKind::kMsrImmediate,             &ArmIrMsrImmediate);
    set(ArmInsnKind::kDataProcessing,           &ArmIrDataProcessing);
    set(ArmInsnKind::kDataProcessingReg,        &ArmIrDataProcessingReg);
    set(ArmInsnKind::kDataProcessingShiftedReg, &ArmIrDataProcessingShiftedReg);
    set(ArmInsnKind::kBranch,                   &ArmIrBranch);
    set(ArmInsnKind::kBx,                       &ArmIrBx);
    set(ArmInsnKind::kBlxReg,                   &ArmIrBlxReg);
    set(ArmInsnKind::kArmBlxImm,                &ArmIrArmBlxImm);
    set(ArmInsnKind::kSvc,                      &ArmIrSvc);
    set(ArmInsnKind::kThumbBlPrefix,            &ArmIrThumbBlPrefix);
    set(ArmInsnKind::kThumbBlSuffix,            &ArmIrThumbBlSuffix);
    set(ArmInsnKind::kThumbBlxImm,              &ArmIrThumbBlxImm);
    set(ArmInsnKind::kCbz,                      &ArmIrCbz);
    set(ArmInsnKind::kTableBranchByte,          &ArmIrTableBranch);
    set(ArmInsnKind::kClrex,                    &ArmIrClrex);
    set(ArmInsnKind::kCps,                      &ArmIrCps);
    set(ArmInsnKind::kMovw,                     &ArmIrMovw);
    set(ArmInsnKind::kMovt,                     &ArmIrMovt);
    set(ArmInsnKind::kCoprocRegisterTransfer,   &ArmIrCoprocRegisterTransfer);
    set(ArmInsnKind::kCoprocExtension,          &ArmIrCoprocExtension);
    set(ArmInsnKind::kCoprocDataTransfer,       &ArmIrCoprocDataTransfer);
    set(ArmInsnKind::kCoprocDataOperation,      &ArmIrCoprocDataOperation);
    set(ArmInsnKind::kNeonData2RegBitcount,         &ArmIrPlanned<&ArmPlanNeonData2RegBitcount>);
    set(ArmInsnKind::kNeonData2RegBitwiseNot,       &ArmIrPlanned<&ArmPlanNeonData2RegBitwiseNot>);
    set(ArmInsnKind::kNeonData2RegCompareZero,      &ArmIrPlanned<&ArmPlanNeonData2RegCompareZero>);
    set(ArmInsnKind::kNeonData2RegCvtHalfSingle,    &ArmIrPlanned<&ArmPlanNeonData2RegCvtHalfSingle>);
    set(ArmInsnKind::kNeonData2RegCvtIntFp,         &ArmIrPlanned<&ArmPlanNeonData2RegCvtIntFp>);
    set(ArmInsnKind::kNeonData2RegNarrow,           &ArmIrPlanned<&ArmPlanNeonData2RegNarrow>);
    set(ArmInsnKind::kNeonData2RegPairwiseAddLong,  &ArmIrPlanned<&ArmPlanNeonData2RegPairwiseAddLong>);
    set(ArmInsnKind::kNeonData2RegReciprocal,       &ArmIrPlanned<&ArmPlanNeonData2RegReciprocal>);
    set(ArmInsnKind::kNeonData2RegReverse,          &ArmIrPlanned<&ArmPlanNeonData2RegReverse>);
    set(ArmInsnKind::kNeonData2RegSatAbsNeg,        &ArmIrPlanned<&ArmPlanNeonData2RegSatAbsNeg>);
    set(ArmInsnKind::kNeonData2RegScalarLong,       &ArmIrPlanned<&ArmPlanNeonData2RegScalarLong>);
    set(ArmInsnKind::kNeonData2RegScalarMul,        &ArmIrPlanned<&ArmPlanNeonData2RegScalarMul>);
    set(ArmInsnKind::kNeonData2RegScalarMulSat,     &ArmIrPlanned<&ArmPlanNeonData2RegScalarMulSat>);
    set(ArmInsnKind::kNeonData2RegScalarMulhSat,    &ArmIrPlanned<&ArmPlanNeonData2RegScalarMulhSat>);
    set(ArmInsnKind::kNeonData2RegShuffle,          &ArmIrPlanned<&ArmPlanNeonData2RegShuffle>);
    set(ArmInsnKind::kNeonData2RegSwap,             &ArmIrPlanned<&ArmPlanNeonData2RegSwap>);
    set(ArmInsnKind::kNeonData2RegUnaryArith,       &ArmIrPlanned<&ArmPlanNeonData2RegUnaryArith>);
    set(ArmInsnKind::kNeonData2RegWiden,            &ArmIrPlanned<&ArmPlanNeonData2RegWiden>);
    set(ArmInsnKind::kNeonData3DiffLen,             &ArmIrPlanned<&ArmPlanNeonData3DiffLen>);
    set(ArmInsnKind::kNeonData3DiffLenAbs,          &ArmIrPlanned<&ArmPlanNeonData3DiffLenAbs>);
    set(ArmInsnKind::kNeonData3DiffLenHn,           &ArmIrPlanned<&ArmPlanNeonData3DiffLenHn>);
    set(ArmInsnKind::kNeonData3DiffLenMul,          &ArmIrPlanned<&ArmPlanNeonData3DiffLenMul>);
    set(ArmInsnKind::kNeonData3DiffLenMulSat,       &ArmIrPlanned<&ArmPlanNeonData3DiffLenMulSat>);
    set(ArmInsnKind::kNeonData3Same,                &ArmIrPlanned<&ArmPlanNeonData3Same>);
    set(ArmInsnKind::kNeonData3SameAcc,             &ArmIrPlanned<&ArmPlanNeonData3SameAcc>);
    set(ArmInsnKind::kNeonData3SameFpAbsCompare,    &ArmIrPlanned<&ArmPlanNeonData3SameFpAbsCompare>);
    set(ArmInsnKind::kNeonData3SameFpArith,         &ArmIrPlanned<&ArmPlanNeonData3SameFpArith>);
    set(ArmInsnKind::kNeonData3SameFpCompare,       &ArmIrPlanned<&ArmPlanNeonData3SameFpCompare>);
    set(ArmInsnKind::kNeonData3SameFpFma,           &ArmIrPlanned<&ArmPlanNeonData3SameFpFma>);
    set(ArmInsnKind::kNeonData3SameFpMinMax,        &ArmIrPlanned<&ArmPlanNeonData3SameFpMinMax>);
    set(ArmInsnKind::kNeonData3SameFpMulAcc,        &ArmIrPlanned<&ArmPlanNeonData3SameFpMulAcc>);
    set(ArmInsnKind::kNeonData3SameFpPairAdd,       &ArmIrPlanned<&ArmPlanNeonData3SameFpPairAdd>);
    set(ArmInsnKind::kNeonData3SameFpPairMinMax,    &ArmIrPlanned<&ArmPlanNeonData3SameFpPairMinMax>);
    set(ArmInsnKind::kNeonData3SameFpRecipStep,     &ArmIrPlanned<&ArmPlanNeonData3SameFpRecipStep>);
    set(ArmInsnKind::kNeonData3SamePairwise,        &ArmIrPlanned<&ArmPlanNeonData3SamePairwise>);
    set(ArmInsnKind::kNeonData3SameSat,             &ArmIrPlanned<&ArmPlanNeonData3SameSat>);
    set(ArmInsnKind::kNeonDataVext,                 &ArmIrPlanned<&ArmPlanNeonDataVext>);
    set(ArmInsnKind::kNeonDataVtbl,                 &ArmIrPlanned<&ArmPlanNeonDataVtbl>);
    set(ArmInsnKind::kNeonLoadAllLanes,             &ArmIrPlanned<&ArmPlanNeonLoadAllLanes>);
    set(ArmInsnKind::kNeonLoadStoreInterleaved,     &ArmIrPlanned<&ArmPlanNeonLoadStoreInterleaved>);
    set(ArmInsnKind::kNeonLoadStoreMultiple,        &ArmIrPlanned<&ArmPlanNeonLoadStoreMultiple>);
    set(ArmInsnKind::kNeonLoadStoreSingleLane,      &ArmIrPlanned<&ArmPlanNeonLoadStoreSingleLane>);
    set(ArmInsnKind::kNeonOneRegImm,                &ArmIrPlanned<&ArmPlanNeonOneRegImm>);
    set(ArmInsnKind::kNeonShiftImm,                 &ArmIrPlanned<&ArmPlanNeonShiftImm>);
    set(ArmInsnKind::kNeonShiftImmNarrow,           &ArmIrPlanned<&ArmPlanNeonShiftImmNarrow>);
    set(ArmInsnKind::kNeonShiftImmNarrowSat,        &ArmIrPlanned<&ArmPlanNeonShiftImmNarrowSat>);
    set(ArmInsnKind::kNeonShiftImmSat,              &ArmIrPlanned<&ArmPlanNeonShiftImmSat>);
    set(ArmInsnKind::kNeonShiftImmWiden,            &ArmIrPlanned<&ArmPlanNeonShiftImmWiden>);
    set(ArmInsnKind::kNeonUnimplemented,            &ArmIrArmUnimplemented);
    set(ArmInsnKind::kNeonVdupScalar,               &ArmIrPlanned<&ArmPlanNeonVdupScalar>);
    return t;
}

constexpr std::array<ArmIrTranslateFn, kArmInsnKindCount> kTranslateTable = BuildTranslateTable();

constexpr bool EveryKindHasTranslator() {
    for (ArmIrTranslateFn fn : kTranslateTable) {
        if (fn == nullptr) return false;
    }
    return true;
}

static_assert(EveryKindHasTranslator(), "an ArmInsnKind has no CerfIR translator");

}

extern const std::array<ArmIrTranslateFn, kArmInsnKindCount> kArmIrTranslateTable =
    kTranslateTable;

}
