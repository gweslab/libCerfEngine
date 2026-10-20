#include <array>
#include <cstddef>

#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

constexpr size_t kOpCount = static_cast<size_t>(Op::kCount);

constexpr std::array<X86LowerFn, kOpCount> BuildLowerTable() {
    std::array<X86LowerFn, kOpCount> t{};
    auto set = [&t](Op op, X86LowerFn fn) { t[static_cast<size_t>(op)] = fn; };
    set(Op::kLabel,          &X86LowerUnsupported);
    set(Op::kBranchIf,       &X86LowerUnsupported);
    set(Op::kBranchIfNot,    &X86LowerUnsupported);
    set(Op::kBranchSlots,    &X86LowerUnsupported);
    set(Op::kGetSlot,        &X86LowerGetSlot);
    set(Op::kSetSlot,        &X86LowerUnsupported);
    set(Op::kAdd,            &X86LowerBinaryOp<Op::kAdd>);
    set(Op::kSub,            &X86LowerBinaryOp<Op::kSub>);
    set(Op::kAdc,            &X86LowerCarryArith);
    set(Op::kSbc,            &X86LowerCarryArith);
    set(Op::kAnd,            &X86LowerBinaryOp<Op::kAnd>);
    set(Op::kOr,             &X86LowerBinaryOp<Op::kOr>);
    set(Op::kXor,            &X86LowerBinaryOp<Op::kXor>);
    set(Op::kAndNot,         &X86LowerInvertedLogic);
    set(Op::kOrNot,          &X86LowerInvertedLogic);
    set(Op::kNot,            &X86LowerUnary);
    set(Op::kCopy,           &X86LowerUnary);
    set(Op::kNeg,            &X86LowerUnary);
    set(Op::kMul,            &X86LowerMul);
    set(Op::kMulU64,         &X86LowerMulWide);
    set(Op::kMulS64,         &X86LowerMulWide);
    set(Op::kDivRemU32,      &X86LowerDivRem);
    set(Op::kDivRemS32,      &X86LowerDivRem);
    set(Op::kPair,           &X86LowerUnsupported);
    set(Op::kLo32,           &X86LowerHalf);
    set(Op::kHi32,           &X86LowerHalf);
    set(Op::kShl,            &X86LowerShiftModulo);
    set(Op::kLsr,            &X86LowerShiftModulo);
    set(Op::kAsr,            &X86LowerShiftModulo);
    set(Op::kRor,            &X86LowerShiftModulo);
    set(Op::kShlC,           &X86LowerShiftCarry);
    set(Op::kLsrC,           &X86LowerShiftCarry);
    set(Op::kAsrC,           &X86LowerShiftCarry);
    set(Op::kRorC,           &X86LowerShiftCarry);
    set(Op::kRrx,            &X86LowerRrx);
    set(Op::kClz,            &X86LowerClz);
    set(Op::kByteSwap,       &X86LowerUnary);
    set(Op::kCmpEq,          &X86LowerCompare);
    set(Op::kCmpNe,          &X86LowerCompare);
    set(Op::kCmpLtS,         &X86LowerCompare);
    set(Op::kCmpLtU,         &X86LowerCompare);
    set(Op::kCmpLeS,         &X86LowerCompare);
    set(Op::kSelect,         &X86LowerSelect);
    set(Op::kZeroExtend,     &X86LowerExtend);
    set(Op::kSignExtend,     &X86LowerExtend);
    set(Op::kTruncate,       &X86LowerUnsupported);
    set(Op::kSatAdd,         &X86LowerSaturate);
    set(Op::kSatSub,         &X86LowerSaturate);
    set(Op::kOrSticky,       &X86LowerOrSticky);
    set(Op::kSignedSat,      &X86LowerSignedSat);
    set(Op::kHostLoad,       &X86LowerHostLoad);
    set(Op::kHostStore,      &X86LowerHostStore);
    set(Op::kHostTest,       &X86LowerHostTest);
    set(Op::kHostReplace,    &X86LowerHostReplace);
    set(Op::kNFlag,          &X86LowerFlag);
    set(Op::kZFlag,          &X86LowerFlag);
    set(Op::kCFlag,          &X86LowerFlag);
    set(Op::kVFlag,          &X86LowerFlag);
    set(Op::kLoad,           &X86LowerLoad);
    set(Op::kStore,          &X86LowerUnsupported);
    set(Op::kSwap,           &X86LowerSwap);
    set(Op::kTransferWords,  &X86LowerUnsupported);
    set(Op::kCallHelper,     &X86LowerCall);
    set(Op::kExitDirect,     &X86LowerUnsupported);
    set(Op::kExitIndirect,   &X86LowerUnsupported);
    set(Op::kExitDispatcher, &X86LowerUnsupported);
    return t;
}

constexpr std::array<X86LowerFn, kOpCount> kLowerTable = BuildLowerTable();

constexpr bool EveryOpHasLowering() {
    for (X86LowerFn fn : kLowerTable) {
        if (fn == nullptr) return false;
    }
    return true;
}

static_assert(EveryOpHasLowering(), "every CerfIR op needs an x86-32 lowering entry");

}

extern const std::array<X86LowerFn, kOpCount> kX86LowerTable = kLowerTable;

}
