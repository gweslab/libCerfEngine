#include "cerfir_builder.h"

#include "cerfir_eval.h"

namespace cerfengine::cerfir {

namespace {

constexpr uint8_t kPrepareValueOp = 0x80u;

constexpr std::array<uint8_t, static_cast<size_t>(Op::kCount)> PrepareMasks() {
    std::array<uint8_t, static_cast<size_t>(Op::kCount)> masks{};
    for (size_t i = 0; i < masks.size(); ++i) {
        const uint32_t flags = kOpInfoTable[i].flags;
        uint8_t        m     = 0u;
        if ((flags & kOpPure) != 0u && (flags & kOpFlagPseudo) == 0u) m |= kPrepareValueOp;
        if ((flags & (kOpArith | kOpNzResult)) != 0u) m |= kWantN | kWantZ;
        if ((flags & (kOpArith | kOpShifterCarry)) != 0u) m |= kWantC;
        if ((flags & kOpArith) != 0u) m |= kWantV;
        masks[i] = m;
    }
    return masks;
}

constexpr std::array<uint8_t, static_cast<size_t>(Op::kCount)> kPrepareMask = PrepareMasks();

bool IsCommutative(Op op) {
    return op == Op::kAdd || op == Op::kAnd || op == Op::kOr || op == Op::kXor ||
           op == Op::kMul;
}

}

Type Builder::ResultType(Op op, Ref a, Ref b) const {
    switch (op) {
    case Op::kCmpEq:
    case Op::kCmpNe:
    case Op::kCmpLtS:
    case Op::kCmpLtU:
    case Op::kCmpLeS: return Type::kI1;
    case Op::kMulU64:
    case Op::kMulS64:
    case Op::kDivRemU32:
    case Op::kDivRemS32:
    case Op::kPair:   return Type::kI64;
    case Op::kLo32:
    case Op::kHi32:   return Type::kI32;
    case Op::kSelect: return block_.TypeOf(b);
    default:          return block_.TypeOf(a);
    }
}

bool Builder::AllConst(Ref a, Ref b, Ref c) const {
    for (Ref r : {a, b, c}) {
        if (r != kNoRef && !IsConstRef(r)) return false;
    }
    return true;
}

Ref Builder::Simplify(Op op, Ref a, Ref b) const {
    if (b == kNoRef) return kNoRef;
    if (IsCommutative(op) && IsConstRef(a) && !IsConstRef(b)) {
        const Ref t = a;
        a = b;
        b = t;
    }
    if (!IsConstRef(b)) return kNoRef;
    const uint64_t vb = block_.ConstValue(b);
    switch (op) {
    case Op::kAdd:
    case Op::kSub:
    case Op::kOr:
    case Op::kXor:
    case Op::kAndNot:
    case Op::kShl:
    case Op::kLsr:
    case Op::kAsr:
    case Op::kRor:
        return vb == 0u ? a : kNoRef;
    case Op::kAnd:
        return vb == TypeMask(block_.TypeOf(a)) ? a : kNoRef;
    case Op::kMul:
        return vb == 1u ? a : kNoRef;
    default:
        return kNoRef;
    }
}

Ref Builder::FoldWithFlags(Op op, Type type, Ref a, Ref b, Ref c, uint8_t want, FlagSet* flags) {
    EvalArgs args{};
    const Ref refs[3] = {a, b, c};
    for (int i = 0; i < 3; ++i) {
        if (refs[i] == kNoRef) continue;
        args.value[i] = block_.ConstValue(refs[i]);
        args.type[i]  = block_.ConstType(refs[i]);
    }
    uint64_t value = 0;
    if (!EvalPure(op, type, args, &value)) return kNoRef;

    const struct { uint8_t bit; Op flag; Ref FlagSet::*slot; } kFlags[] = {
        {kWantN, Op::kNFlag, &FlagSet::n}, {kWantZ, Op::kZFlag, &FlagSet::z},
        {kWantC, Op::kCFlag, &FlagSet::c}, {kWantV, Op::kVFlag, &FlagSet::v},
    };
    for (const auto& f : kFlags) {
        if ((want & f.bit) == 0u) continue;
        uint64_t bit = 0;
        if (!EvalFlag(f.flag, op, type, args, &bit)) Reject("flag fold failed", op);
        flags->*f.slot = Const1(bit != 0u);
    }
    return block_.Const(type, value);
}

Ref Builder::Wide64(Op op, Ref a, Ref b) {
    if (op != Op::kMulS64 && op != Op::kMulU64 && op != Op::kDivRemU32 && op != Op::kDivRemS32) {
        NotAWide64Op(op);
    }
    if (IsConstRef(a) && IsConstRef(b)) return Compute(op, a, b, kNoRef, 0u, nullptr);
    return Emit(op, Type::kI64, a, b, kNoRef, 0u, 0u);
}

Ref Builder::ComputeFlags(Op op, Ref a, Ref b, Ref c, uint8_t want, FlagSet* flags,
                          bool keep_value) {
    Type      type = Type::kVoid;
    const Ref done = Prepare(op, a, b, c, want, flags, &type);
    if (done != kNoRef) {
        if (keep_value) return done;
        Drop(done);
        return kNoRef;
    }
    const uint16_t uses = static_cast<uint16_t>((keep_value ? 1u : 0u) + kFlagCount[want & 0x0Fu]);
    const Ref r = Build(op, type, a, b, c, 0u, static_cast<uint16_t>(want << 4), kNoDest, 0u, uses);
    if ((want & kWantN) != 0u) flags->n = Emit(Op::kNFlag, Type::kI1, r, kNoRef, kNoRef, 0u, 0u);
    if ((want & kWantZ) != 0u) flags->z = Emit(Op::kZFlag, Type::kI1, r, kNoRef, kNoRef, 0u, 0u);
    if ((want & kWantC) != 0u) flags->c = Emit(Op::kCFlag, Type::kI1, r, kNoRef, kNoRef, 0u, 0u);
    if ((want & kWantV) != 0u) flags->v = Emit(Op::kVFlag, Type::kI1, r, kNoRef, kNoRef, 0u, 0u);
    return keep_value ? r : kNoRef;
}

void Builder::ComputeStoreFlags(Op op, Ref a, Ref b, Ref c, uint8_t store) {
    if (store == 0u) Reject("no flag to store", op);
    ComputeInto(op, a, b, c, store, kNoDest);
}

Ref Builder::ComputeIntoSlow(Op op, Ref a, Ref b, Ref c, uint8_t store, uint32_t dest,
                             bool sext) {
    if (store != 0u && !block_.FlagSlotsOf().set) Reject("flag store without flag slots", op);
    if (sext && (store != 0u || dest == kNoDest)) {
        Reject("sign-extended destination without a slot or with a flag store", op);
    }
    FlagSet   f;
    Type      type = Type::kVoid;
    const Ref done = Prepare(op, a, b, c, store, store != 0u ? &f : nullptr, &type);
    if (done != kNoRef) {
        const int32_t* disp = block_.FlagSlotsOf().disp;
        if ((store & kWantN) != 0u) SetSlot(static_cast<uint32_t>(disp[0]), f.n);
        if ((store & kWantZ) != 0u) SetSlot(static_cast<uint32_t>(disp[1]), f.z);
        if ((store & kWantC) != 0u) SetSlot(static_cast<uint32_t>(disp[2]), f.c);
        if ((store & kWantV) != 0u) SetSlot(static_cast<uint32_t>(disp[3]), f.v);
        if (dest == kNoDest && store == 0u) return done;
        if (dest == kNoDest) {
            Drop(done);
        } else if (sext) {
            SetSlotSext(dest, done);
        } else {
            SetSlot(dest, done);
        }
        return kNoRef;
    }
    if (dest == kNoDest && store == 0u) return Build(op, type, a, b, c, 0u, 0u, kNoDest, 0u, 1u);
    if (dest != kNoDest && (type != Type::kI32 || dest > kNoDest)) {
        Reject("destination slot of a non-32-bit value", op);
    }
    const uint8_t  meta = static_cast<uint8_t>((store != 0u ? kValueStoresFlags : 0u) |
                                               (sext ? kValueDestSext : 0u));
    const uint16_t uses = static_cast<uint16_t>((dest != kNoDest ? 1u : 0u) + (store != 0u ? 1u : 0u));
    Build(op, type, a, b, c, 0u, store, static_cast<uint16_t>(dest), meta, uses);
    if (dest != kNoDest) NoteSlotWrite(static_cast<int32_t>(dest), sext ? 8u : 4u);
    if (store != 0u) NoteFlagWrites(store);
    return kNoRef;
}

Ref Builder::Prepare(Op op, Ref a, Ref b, Ref c, uint8_t want, FlagSet* flags, Type* type_out) {
    const uint8_t mask = kPrepareMask[static_cast<size_t>(op)];
    if ((mask & kPrepareValueOp) == 0u) Reject("not a value op", op);
    const uint32_t present = (a != kNoRef ? 1u : 0u) | (b != kNoRef ? 2u : 0u) |
                             (c != kNoRef ? 4u : 0u);
    if (present != (1u << InfoOf(op).num_args) - 1u) Reject("argument count mismatch", op);
    if (want != 0u && flags == nullptr) Reject("flags wanted without a flag set", op);
    if ((want & ~mask & 0x0Fu) != 0u) Reject("flag not defined for the producer", op);

    const bool shift_c = op == Op::kShlC || op == Op::kLsrC || op == Op::kAsrC ||
                         op == Op::kRorC;
    if (shift_c && IsConstRef(b) && block_.ConstValue(b) == 0u) {
        /* ARM DDI 0406C.c A8.4.3 Shift_C() (p. A8-292): amount == 0 gives
           (value, carry_in). */
        const uint8_t nz    = want & static_cast<uint8_t>(kWantN | kWantZ);
        const Ref     value = nz != 0u ? Compute(Op::kCopy, a, kNoRef, kNoRef, nz, flags) : a;
        if ((want & kWantC) != 0u) {
            flags->c = c;
        } else {
            Drop(c);
        }
        return value;
    }

    const Type type = ResultType(op, a, b);
    *type_out       = type;
    if (AllConst(a, b, c)) {
        const Ref folded = FoldWithFlags(op, type, a, b, c, want, flags);
        if (folded != kNoRef) return folded;
    }
    if (want == 0u) {
        if (op == Op::kCopy) return a;
        if ((op == Op::kLo32 || op == Op::kHi32) && IsValueRef(a) && block_.At(a).op == Op::kPair) {
            Drop(block_.At(a).args[op == Op::kHi32 ? 0 : 1]);
            return block_.At(a).args[op == Op::kHi32 ? 1 : 0];
        }
        if (op == Op::kSelect && IsConstRef(a)) {
            const bool take_b = (block_.ConstValue(a) & 1u) != 0u;
            Drop(take_b ? c : b);
            return take_b ? b : c;
        }
        if (op == Op::kSelect && b == c) {
            Drop(a);
            Drop(c);
            return b;
        }
        const Ref same = Simplify(op, a, b);
        if (same != kNoRef) return same;
    }
    return kNoRef;
}

Ref Builder::ConvertConst(Op op, Type to, Ref a) {
    EvalArgs args{};
    args.value[0] = block_.ConstValue(a);
    args.type[0]  = block_.ConstType(a);
    uint64_t value = 0;
    if (EvalPure(op, to, args, &value)) return block_.Const(to, value);
    return Emit(op, to, a, kNoRef, kNoRef, 0u, 0u);
}

void Builder::NotAConversion(Op op) {
    Reject("not a conversion", op);
}

void Builder::BranchNotI1() {
    Reject("branch condition is not i1", Op::kBranchIf);
}

void Builder::NotACompare(Op op) {
    Reject("not a compare op", op);
}

void Builder::NotAWide64Op(Op op) {
    Reject("not a 32-by-32 op with a 64-bit result", op);
}

void Builder::CallShapeFault() {
    Reject("helper call with gapped arguments or a pc store without a pc slot", Op::kCallHelper);
}

Ref Builder::SlotTestValue(const SlotTest& t) {
    const Op  op = t.if_true ? Op::kCmpEq : Op::kCmpNe;
    const Ref a  = ReadSlot(static_cast<uint32_t>(t.disp[0]));
    switch (t.pred) {
    case SlotPredicate::kZero32:
        return Compare(op, a, Const32(0u));
    case SlotPredicate::kEqual32:
        return Compare(op, a, ReadSlot(static_cast<uint32_t>(t.disp[1])));
    case SlotPredicate::kZero64:
        return Compare(op, Pair(a, ReadSlot(static_cast<uint32_t>(t.disp[0] + 4))),
                       Const(Type::kI64, 0u));
    case SlotPredicate::kEqual64:
        return Compare(op, Pair(a, ReadSlot(static_cast<uint32_t>(t.disp[0] + 4))),
                       Pair(ReadSlot(static_cast<uint32_t>(t.disp[1])),
                            ReadSlot(static_cast<uint32_t>(t.disp[1] + 4))));
    default:
        Reject("slot predicate has no value form", Op::kBranchSlots);
    }
}

void Builder::ConstCallFault() {
    Reject("a constant-argument helper call with a result, more than four arguments, a host-word "
           "argument or a pc store without a pc slot",
           Op::kCallHelper);
}

void Builder::CallConstRecorded(const CallSpec& spec, uint32_t num_args, CallArg a0, CallArg a1,
                                CallArg a2, CallArg a3) {
    if (num_args <= 3u) {
        Call(spec, num_args > 0u ? ConstArg(a0) : kNoRef, num_args > 1u ? ConstArg(a1) : kNoRef,
             num_args > 2u ? ConstArg(a2) : kNoRef);
        return;
    }
    CallDesc call{spec.fn, spec.conv, 3u};
    call.num_extra = 1u;
    call.extra[0]  = a3;
    call.store_pc  = spec.store_pc;
    call.pc        = spec.pc;
    if (spec.store_pc) NoteSlotWrite(block_.PcSlot(), 4u);
    Call(call, Type::kVoid, ConstArg(a0), ConstArg(a1), ConstArg(a2));
}

Ref Builder::Saturate(Op op, Ref a, Ref b, uint32_t sticky_slot, uint32_t sticky_mask) {
    if (op != Op::kSatAdd && op != Op::kSatSub && op != Op::kSignedSat) {
        Reject("not a saturating op", op);
    }
    const Ref r = Emit(op, Type::kI32, a, b, Const32(sticky_mask), sticky_slot, 0u);
    NoteSlotWrite(static_cast<int32_t>(sticky_slot), 4u);
    return r;
}

void Builder::OrSticky(Ref cond, uint32_t sticky_slot, uint32_t sticky_mask) {
    if (block_.TypeOf(cond) != Type::kI1) Reject("sticky condition is not i1", Op::kOrSticky);
    if (IsConstRef(cond) && block_.ConstValue(cond) == 0u) return;
    Emit(Op::kOrSticky, Type::kVoid, cond, Const32(sticky_mask), kNoRef, sticky_slot, 0u);
    NoteSlotWrite(static_cast<int32_t>(sticky_slot), 4u);
}

Ref Builder::Call(const CallDesc& call, Type result, Ref a, Ref b, Ref c) {
    const Ref refs[3] = {a, b, c};
    for (uint32_t i = 0; i < 3u; ++i) {
        if ((refs[i] != kNoRef) != (i < call.num_args)) {
            Reject("helper argument count mismatch", Op::kCallHelper);
        }
    }
    if (call.num_extra > kMaxCallExtraArgs) Reject("too many extra helper arguments", Op::kCallHelper);
    if (DirectCallShape(result, a, b, c)) {
        DirectCall(call.fn, call.conv, call.num_args, a, b, c, call.extra, call.num_extra,
                   call.store_pc, call.pc);
        return kNoRef;
    }
    return Emit(Op::kCallHelper, result, a, b, c, block_.AddCall(call), 0u);
}

Ref Builder::HostLoad(const void* address, uint8_t width, bool sign_extend) {
    const uint16_t shape = static_cast<uint16_t>(width | (sign_extend ? kHostSigned : 0u));
    return Emit(Op::kHostLoad, Type::kI32, ConstPtr(address), kNoRef, kNoRef, 0u, shape);
}

void Builder::HostStore(const void* address, Ref value, uint8_t width) {
    Emit(Op::kHostStore, Type::kVoid, ConstPtr(address), value, kNoRef, 0u, width);
}

Ref Builder::HostTest(const void* address, uint32_t mask) {
    return Emit(Op::kHostTest, Type::kI1, ConstPtr(address), Const32(mask), kNoRef, 0u, 0u);
}

Ref Builder::HostReplace(const void* address, Ref value) {
    return Emit(Op::kHostReplace, Type::kI1, ConstPtr(address), value, kNoRef, 0u, 0u);
}

void Builder::ExitDirectFault(uint16_t chain_slot) {
    if ((chain_slot & ~kExitSlotMask) != 0u) Reject("chain slot uses a flag bit", Op::kExitDirect);
    Reject("pc store without a pc slot", Op::kExitDirect);
}

}
