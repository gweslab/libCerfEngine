#include <array>
#include <cstddef>

#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

constexpr size_t kOpCount = static_cast<size_t>(Op::kCount);

constexpr std::array<A64LowerFn, kOpCount> BuildLowerTable() {
    std::array<A64LowerFn, kOpCount> t{};
    auto set = [&t](Op op, A64LowerFn fn) { t[static_cast<size_t>(op)] = fn; };
    set(Op::kLabel,          &A64LowerUnsupported);
    set(Op::kBranchIf,       &A64LowerUnsupported);
    set(Op::kBranchIfNot,    &A64LowerUnsupported);
    set(Op::kBranchSlots,    &A64LowerUnsupported);
    set(Op::kGetSlot,        &A64LowerGetSlot);
    set(Op::kSetSlot,        &A64LowerUnsupported);
    set(Op::kAdd,            &A64LowerBinaryOp<Op::kAdd>);
    set(Op::kSub,            &A64LowerBinaryOp<Op::kSub>);
    set(Op::kAdc,            &A64LowerCarryArith);
    set(Op::kSbc,            &A64LowerCarryArith);
    set(Op::kAnd,            &A64LowerBinaryOp<Op::kAnd>);
    set(Op::kOr,             &A64LowerBinaryOp<Op::kOr>);
    set(Op::kXor,            &A64LowerBinaryOp<Op::kXor>);
    set(Op::kAndNot,         &A64LowerInvertedLogic);
    set(Op::kOrNot,          &A64LowerInvertedLogic);
    set(Op::kNot,            &A64LowerUnary);
    set(Op::kCopy,           &A64LowerUnary);
    set(Op::kNeg,            &A64LowerUnary);
    set(Op::kMul,            &A64LowerMul);
    set(Op::kMulU64,         &A64LowerMulWide);
    set(Op::kMulS64,         &A64LowerMulWide);
    set(Op::kDivRemU32,      &A64LowerDivRem);
    set(Op::kDivRemS32,      &A64LowerDivRem);
    set(Op::kPair,           &A64LowerUnsupported);
    set(Op::kLo32,           &A64LowerHalf);
    set(Op::kHi32,           &A64LowerHalf);
    set(Op::kShl,            &A64LowerShiftModulo);
    set(Op::kLsr,            &A64LowerShiftModulo);
    set(Op::kAsr,            &A64LowerShiftModulo);
    set(Op::kRor,            &A64LowerShiftModulo);
    set(Op::kShlC,           &A64LowerShiftCarry);
    set(Op::kLsrC,           &A64LowerShiftCarry);
    set(Op::kAsrC,           &A64LowerShiftCarry);
    set(Op::kRorC,           &A64LowerShiftCarry);
    set(Op::kRrx,            &A64LowerRrx);
    set(Op::kClz,            &A64LowerClz);
    set(Op::kByteSwap,       &A64LowerUnary);
    set(Op::kCmpEq,          &A64LowerCompare);
    set(Op::kCmpNe,          &A64LowerCompare);
    set(Op::kCmpLtS,         &A64LowerCompare);
    set(Op::kCmpLtU,         &A64LowerCompare);
    set(Op::kCmpLeS,         &A64LowerCompare);
    set(Op::kSelect,         &A64LowerSelect);
    set(Op::kZeroExtend,     &A64LowerExtend);
    set(Op::kSignExtend,     &A64LowerExtend);
    set(Op::kTruncate,       &A64LowerUnsupported);
    set(Op::kSatAdd,         &A64LowerSaturate);
    set(Op::kSatSub,         &A64LowerSaturate);
    set(Op::kOrSticky,       &A64LowerOrSticky);
    set(Op::kSignedSat,      &A64LowerSignedSat);
    set(Op::kHostLoad,       &A64LowerHostLoad);
    set(Op::kHostStore,      &A64LowerHostStore);
    set(Op::kHostTest,       &A64LowerHostTest);
    set(Op::kHostReplace,    &A64LowerHostReplace);
    set(Op::kNFlag,          &A64LowerFlag);
    set(Op::kZFlag,          &A64LowerFlag);
    set(Op::kCFlag,          &A64LowerFlag);
    set(Op::kVFlag,          &A64LowerFlag);
    set(Op::kLoad,           &A64LowerLoad);
    set(Op::kStore,          &A64LowerUnsupported);
    set(Op::kSwap,           &A64LowerSwap);
    set(Op::kTransferWords,  &A64LowerUnsupported);
    set(Op::kCallHelper,     &A64LowerCall);
    set(Op::kExitDirect,     &A64LowerUnsupported);
    set(Op::kExitIndirect,   &A64LowerUnsupported);
    set(Op::kExitDispatcher, &A64LowerUnsupported);
    return t;
}

constexpr std::array<A64LowerFn, kOpCount> kLowerTable = BuildLowerTable();

constexpr bool EveryOpHasLowering() {
    for (A64LowerFn fn : kLowerTable) {
        if (fn == nullptr) return false;
    }
    return true;
}

static_assert(EveryOpHasLowering(), "every CerfIR op needs an A64 lowering entry");

}

extern const std::array<A64LowerFn, kOpCount> kA64LowerTable = kLowerTable;

}
