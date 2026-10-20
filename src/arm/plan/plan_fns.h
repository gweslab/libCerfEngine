#pragma once

#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/decoded_insn.h>

#include "../arm_call_plan.h"

namespace cerfengine {

ArmCallPlan ArmPlanNeonData2RegBitcount(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegBitwiseNot(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegCompareZero(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegCvtHalfSingle(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegCvtIntFp(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegNarrow(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegPairwiseAddLong(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegReciprocal(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegReverse(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegSatAbsNeg(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegScalarLong(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegScalarMul(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegScalarMulSat(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegScalarMulhSat(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegShuffle(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegSwap(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegUnaryArith(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData2RegWiden(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3DiffLen(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3DiffLenAbs(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3DiffLenHn(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3DiffLenMul(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3DiffLenMulSat(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3Same(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameAcc(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameFpAbsCompare(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameFpArith(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameFpCompare(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameFpFma(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameFpMinMax(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameFpMulAcc(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameFpPairAdd(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameFpPairMinMax(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameFpRecipStep(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SamePairwise(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonData3SameSat(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonDataVext(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonDataVtbl(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonLoadAllLanes(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonLoadStoreInterleaved(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonLoadStoreMultiple(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonLoadStoreSingleLane(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonOneRegImm(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonShiftImm(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonShiftImmNarrow(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonShiftImmNarrowSat(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonShiftImmSat(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonShiftImmWiden(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonVdupScalar(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonVdupCore(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonCoreToScalar(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanNeonScalarToCore(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanVfpDataOperation(const DecodedInsn& d, const BlockContext& ctx);
ArmCallPlan ArmPlanVfpDataTransfer(const DecodedInsn& d, const BlockContext& ctx);

}
