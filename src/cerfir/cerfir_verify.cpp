#include "cerfir_verify.h"

#include <cstdio>
#include <vector>

namespace cerfengine::cerfir {

namespace {

constexpr uint32_t kNoLabel = 0xFFFFFFFFu;

class Verifier {
public:
    Verifier(const Block& block, std::string* error) : b_(block), error_(error) {}

    bool Run();

private:
    bool Fail(uint32_t at, const char* what);
    bool CheckArgs(uint32_t at, const Inst& inst);
    bool CheckTypes(uint32_t at, const Inst& inst);
    bool CheckFlag(uint32_t at, const Inst& inst);
    bool CheckFlagStore(uint32_t at, const Inst& inst);
    Type T(Ref r) const { return b_.TypeOf(r); }
    uint32_t Bits(Ref r) const { return TypeBits(T(r)); }

    const Block&          b_;
    std::string*          error_;
    std::vector<uint32_t> label_at_;
    uint32_t              region_start_ = 0;
};

bool Verifier::Fail(uint32_t at, const char* what) {
    if (error_ != nullptr) {
        char text[192];
        std::snprintf(text, sizeof(text), "CerfIR verify: %%%u %s: %s", at,
                      at < b_.Size() ? InfoOf(b_.At(static_cast<Ref>(at)).op).name : "-", what);
        *error_ = text;
    }
    return false;
}

bool Verifier::CheckArgs(uint32_t at, const Inst& inst) {
    uint32_t expected = InfoOf(inst.op).num_args;
    if (inst.op == Op::kCallHelper) {
        expected = b_.CallAt(inst.aux).num_args;
        if (b_.CallAt(inst.aux).store_pc && b_.PcSlot() == kNoSlot) {
            return Fail(at, "pc store without a pc slot");
        }
    }
    uint32_t mask = (1u << expected) - 1u;
    if (IsMemoryOp(inst.op)) mask = MemoryArgMask(inst.op, b_.MemAttrAt(inst.aux));
    for (uint32_t i = 0; i < 3u; ++i) {
        const Ref r = inst.args[i];
        if ((r != kNoRef) != (((mask >> i) & 1u) != 0u)) return Fail(at, "argument count");
        if (r == kNoRef) continue;
        if (IsConstRef(r)) {
            if (ConstIndex(r) >= b_.ConstCount()) return Fail(at, "constant out of range");
            continue;
        }
        if (IsSlotRef(r)) {
            if (((kSlotArgMask[static_cast<size_t>(inst.op)] >> i) & 1u) == 0u) {
                return Fail(at, "slot operand in a position that takes none");
            }
            if ((SlotRefDisp(r) & 3) != 0) return Fail(at, "slot operand not word aligned");
            continue;
        }
        if (r >= at) return Fail(at, "argument defined at or after its use");
        if (r < region_start_) return Fail(at, "value crosses a label");
        if (b_.At(r).type == Type::kVoid) return Fail(at, "argument has no value");
    }
    return true;
}

bool Verifier::CheckFlag(uint32_t at, const Inst& inst) {
    const Ref producer = inst.args[0];
    if (IsConstRef(producer)) return Fail(at, "flag of a constant");
    if (!FlagDefinedFor(inst.op, b_.At(producer).op)) return Fail(at, "flag not defined");
    const Inst& prev = b_.At(static_cast<Ref>(at - 1u));
    const bool adjacent = producer == at - 1u ||
                          (OpHas(prev.op, kOpFlagPseudo) && prev.args[0] == producer) ||
                          (Block::HiHalf(producer) == at - 1u && b_.Info(producer).wide != 0u);
    return adjacent ? true : Fail(at, "flag does not follow its producer");
}

bool Verifier::CheckFlagStore(uint32_t at, const Inst& inst) {
    const uint32_t store = inst.aux16 & 0x0Fu;
    if (inst.aux16 > 0xFFu) return Fail(at, "flag mask");
    if (store != 0u && !b_.FlagSlotsOf().set) return Fail(at, "flag store without flag slots");
    if (store != 0u && inst.type != Type::kI32) return Fail(at, "flag store of a non-32-bit value");
    static constexpr Op kFlags[4] = {Op::kNFlag, Op::kZFlag, Op::kCFlag, Op::kVFlag};
    const uint32_t flags = store | (inst.aux16 >> 4);
    for (uint32_t i = 0; i < 4u; ++i) {
        if ((flags & (1u << i)) != 0u && !FlagDefinedFor(kFlags[i], inst.op)) {
            return Fail(at, "flag not defined");
        }
    }
    return true;
}

bool Verifier::CheckTypes(uint32_t at, const Inst& inst) {
    const Ref* a = inst.args;
    const Type t = inst.type;
    switch (inst.op) {
    case Op::kAdd: case Op::kSub: case Op::kAnd: case Op::kOr: case Op::kXor:
    case Op::kAndNot: case Op::kOrNot: case Op::kMul:
        return (T(a[0]) == t && T(a[1]) == t) ? true : Fail(at, "operand types");
    case Op::kAdc: case Op::kSbc:
        return (T(a[0]) == t && T(a[1]) == t && T(a[2]) == Type::kI1)
                   ? true : Fail(at, "operand types");
    case Op::kNot: case Op::kNeg: case Op::kCopy:
        return T(a[0]) == t ? true : Fail(at, "operand type");
    case Op::kClz: case Op::kByteSwap:
        return (t == Type::kI32 && T(a[0]) == t) ? true : Fail(at, "operand type");
    case Op::kMulU64: case Op::kMulS64: case Op::kDivRemU32: case Op::kDivRemS32: case Op::kPair:
        return (t == Type::kI64 && T(a[0]) == Type::kI32 && T(a[1]) == Type::kI32)
                   ? true : Fail(at, "operand types");
    case Op::kLo32: case Op::kHi32:
        return (t == Type::kI32 && T(a[0]) == Type::kI64) ? true : Fail(at, "operand type");
    case Op::kShl: case Op::kLsr: case Op::kAsr: case Op::kRor:
        return ((t == Type::kI32 || t == Type::kI64) && T(a[0]) == t && T(a[1]) == Type::kI32)
                   ? true : Fail(at, "operand types");
    case Op::kShlC: case Op::kLsrC: case Op::kAsrC: case Op::kRorC:
        return (t == Type::kI32 && T(a[0]) == t && T(a[1]) == Type::kI32 &&
                T(a[2]) == Type::kI1) ? true : Fail(at, "operand types");
    case Op::kRrx:
        return (t == Type::kI32 && T(a[0]) == t && T(a[1]) == Type::kI1)
                   ? true : Fail(at, "operand types");
    case Op::kCmpEq: case Op::kCmpNe:
        return (t == Type::kI1 && T(a[0]) == T(a[1])) ? true : Fail(at, "operand types");
    case Op::kCmpLtS: case Op::kCmpLtU: case Op::kCmpLeS:
        return (t == Type::kI1 && T(a[0]) == T(a[1]) &&
                (T(a[0]) == Type::kI32 || T(a[0]) == Type::kI64))
                   ? true : Fail(at, "operand types");
    case Op::kSelect:
        return (T(a[0]) == Type::kI1 && T(a[1]) == t && T(a[2]) == t)
                   ? true : Fail(at, "operand types");
    case Op::kZeroExtend: case Op::kSignExtend:
        return Bits(a[0]) < TypeBits(t) ? true : Fail(at, "extension does not widen");
    case Op::kTruncate:
        return Bits(a[0]) > TypeBits(t) ? true : Fail(at, "truncation does not narrow");
    case Op::kOrSticky:
        return (T(a[0]) == Type::kI1 && IsConstRef(a[1]) && T(a[1]) == Type::kI32)
                   ? true : Fail(at, "sticky operand types");
    case Op::kHostLoad:
        return (t == Type::kI32 && IsConstRef(a[0]) && T(a[0]) == Type::kPtr)
                   ? true : Fail(at, "host load operand types");
    case Op::kHostStore:
        return (IsConstRef(a[0]) && T(a[0]) == Type::kPtr && T(a[1]) == Type::kI32)
                   ? true : Fail(at, "host store operand types");
    case Op::kHostTest:
        return (t == Type::kI1 && IsConstRef(a[0]) && T(a[0]) == Type::kPtr &&
                IsConstRef(a[1]) && T(a[1]) == Type::kI32)
                   ? true : Fail(at, "host test operand types");
    case Op::kHostReplace:
        return (t == Type::kI1 && IsConstRef(a[0]) && T(a[0]) == Type::kPtr &&
                T(a[1]) == Type::kI32)
                   ? true : Fail(at, "host replace operand types");
    case Op::kSignedSat:
        return (t == Type::kI32 && T(a[0]) == t && IsConstRef(a[1]) && T(a[1]) == Type::kI32 &&
                b_.ConstValue(a[1]) >= 1u && b_.ConstValue(a[1]) <= 31u && IsConstRef(a[2]) &&
                T(a[2]) == Type::kI32) ? true : Fail(at, "saturation operand types");
    case Op::kSatAdd: case Op::kSatSub:
        return (t == Type::kI32 && T(a[0]) == t && T(a[1]) == t && IsConstRef(a[2]) &&
                T(a[2]) == Type::kI32) ? true : Fail(at, "saturation operand types");
    case Op::kNFlag: case Op::kZFlag: case Op::kCFlag: case Op::kVFlag:
        return t == Type::kI1 ? CheckFlag(at, inst) : Fail(at, "flag type");
    case Op::kBranchIf: case Op::kBranchIfNot:
        return T(a[0]) == Type::kI1 ? true : Fail(at, "branch condition type");
    case Op::kGetSlot:
        return t != Type::kVoid ? true : Fail(at, "slot type");
    case Op::kLoad: {
        const MemAttr& m = b_.MemAttrAt(inst.aux);
        return ((a[0] == kNoRef || T(a[0]) == Type::kI32) && TypeBits(t) >= m.width_bytes * 8u &&
                (a[1] == kNoRef || T(a[1]) == Type::kI32))
                   ? true : Fail(at, "load types");
    }
    case Op::kStore: {
        const MemAttr& m = b_.MemAttrAt(inst.aux);
        return ((a[0] == kNoRef || T(a[0]) == Type::kI32) && Bits(a[1]) >= m.width_bytes * 8u &&
                (a[2] == kNoRef || T(a[2]) == Type::kI32))
                   ? true : Fail(at, "store types");
    }
    case Op::kSwap: {
        const MemAttr& m = b_.MemAttrAt(inst.aux);
        return (T(a[0]) == Type::kI32 && T(a[1]) == t && TypeBits(t) >= m.width_bytes * 8u)
                   ? true : Fail(at, "swap types");
    }
    case Op::kTransferWords: {
        const MemAttr& m = b_.MemAttrAt(inst.aux);
        const uint32_t n = b_.WordListAt(inst.aux16).count;
        return (m.base_slot != kNoSlot && m.width_bytes == 4u && m.complete && n != 0u &&
                n <= kMaxWords)
                   ? true : Fail(at, "word transfer shape");
    }
    default:
        return true;
    }
}

bool Verifier::Run() {
    if (b_.Size() == 0u) return Fail(0u, "empty block");
    label_at_.assign(b_.LabelCount(), kNoLabel);
    for (uint32_t i = 0; i < b_.Size(); ++i) {
        const Inst& inst = b_.At(static_cast<Ref>(i));
        if (inst.op != Op::kLabel) continue;
        if (inst.aux >= label_at_.size()) return Fail(i, "label id out of range");
        if (label_at_[inst.aux] != kNoLabel) return Fail(i, "label defined twice");
        label_at_[inst.aux] = i;
    }

    bool after_terminal = false;
    for (uint32_t i = 0; i < b_.Size(); ++i) {
        const Inst& inst = b_.At(static_cast<Ref>(i));
        if (inst.op == Op::kLabel) {
            region_start_  = i + 1u;
            after_terminal = false;
            continue;
        }
        if (after_terminal) return Fail(i, "code after a terminal");
        if (!CheckArgs(i, inst) || !CheckTypes(i, inst)) return false;
        if (OpHas(inst.op, kOpPure) && !OpHas(inst.op, kOpFlagPseudo) && inst.aux16 != 0u &&
            !CheckFlagStore(i, inst)) {
            return false;
        }
        if (inst.dest != kNoDest &&
            (inst.type != Type::kI32 || !OpHas(inst.op, kOpPure) || OpHas(inst.op, kOpFlagPseudo))) {
            return Fail(i, "destination slot on an op other than a 32-bit value op");
        }
        if ((b_.Info(static_cast<Ref>(i)).meta & kValueDestSext) != 0u &&
            (inst.dest == kNoDest || inst.aux16 != 0u)) {
            return Fail(i, "sign-extended destination without a slot or with a flag store");
        }
        if (inst.op == Op::kSetSlot && inst.aux16 != 0u &&
            (inst.aux16 != kSetSlotSext || b_.TypeOf(inst.args[0]) != Type::kI32)) {
            return Fail(i, "sign-extended slot store of a value other than i32");
        }
        if (inst.op == Op::kBranchSlots && inst.aux16 >= b_.SlotTestCount()) {
            return Fail(i, "slot test out of range");
        }
        if (inst.op == Op::kExitDirect && (inst.aux16 & kExitStorePc) != 0u &&
            b_.PcSlot() == kNoSlot) {
            return Fail(i, "pc store without a pc slot");
        }
        if (inst.op == Op::kBranchIf || inst.op == Op::kBranchIfNot ||
            inst.op == Op::kBranchSlots) {
            if (inst.aux >= label_at_.size() || label_at_[inst.aux] == kNoLabel) {
                return Fail(i, "branch to an undefined label");
            }
            if (label_at_[inst.aux] < i) return Fail(i, "backward branch");
        }
        after_terminal = OpHas(inst.op, kOpTerminal);
        if (inst.type != Type::kVoid && inst.op != Op::kPair && inst.op != Op::kTruncate &&
            b_.Info(static_cast<Ref>(i)).uses != 0u) {
            return Fail(i, "value is never consumed");
        }
    }
    const Inst& last = b_.At(static_cast<Ref>(b_.Size() - 1u));
    return OpHas(last.op, kOpTerminal) ? true : Fail(b_.Size() - 1u, "block does not end in a terminal");
}

}

bool Verify(const Block& block, std::string* error) {
    return Verifier(block, error).Run();
}

}
