#include "sh_dsp_alu.h"

#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>

namespace cerfengine {

CERFENGINE_REGISTER(ShDspAlu);

namespace {

constexpr uint32_t kDsrDc      = 1u << 0;
constexpr uint32_t kDsrCs      = 7u << 1;
constexpr uint32_t kDsrCsShift = 1u;
constexpr uint32_t kDsrV       = 1u << 4;
constexpr uint32_t kDsrN       = 1u << 5;
constexpr uint32_t kDsrZ       = 1u << 6;
constexpr uint32_t kDsrGt      = 1u << 7;
constexpr uint32_t kGuardMask  = 0xFFu;
constexpr uint32_t kWordSign   = 0x8000u;

enum class DcMode : uint32_t { kCarry, kNegative, kZero, kOverflow, kGreater, kGreaterEqual };

constexpr ShDspReg Sx(uint32_t b) { return kShDspSx[(b >> 6) & 3u]; }
constexpr ShDspReg Sy(uint32_t b) { return kShDspSy[(b >> 4) & 3u]; }

constexpr bool WritesAlu(ShDspFieldB f) {
    return f.form != ShDspForm::kSxSy && f.form != ShDspForm::kMultiply && f.op != ShDspOp::kPldsMach &&
           f.op != ShDspOp::kPldsMacl;
}

}

void ShDspAlu::OnReady() {
    state_ = engine_.Get<ShCpu>().State();
}

bool ShDspAlu::Saturating() const {
    return (state_->sr & kShSrS) != 0u;
}

uint32_t ShDspAlu::DcBit() const {
    return state_->dsr & kDsrDc;
}

uint32_t& ShDspAlu::Slot(ShDspReg reg) const {
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(state_) + ShDspRegOffset(reg));
}

uint32_t ShDspAlu::Word(ShDspReg reg) const {
    return Slot(reg);
}

uint32_t* ShDspAlu::Guard(ShDspReg reg) const {
    return ShDspHasGuard(reg) ? &Slot(ShDspGuardOf(reg)) : nullptr;
}

int64_t ShDspAlu::Source(ShDspReg reg) const {
    const uint32_t* guard = Guard(reg);
    if (guard == nullptr) return static_cast<int32_t>(Word(reg));
    return ShDspSext40((static_cast<uint64_t>(*guard & kGuardMask) << 32) | Word(reg));
}

void ShDspAlu::Write(ShDspReg reg, int64_t value) {
    Slot(reg)       = static_cast<uint32_t>(value);
    uint32_t* guard = Guard(reg);
    if (guard != nullptr) *guard = static_cast<uint32_t>(static_cast<uint64_t>(value) >> 32) & kGuardMask;
}

ShDspAlu::Flags ShDspAlu::ArithmeticFlags(const ShDspResult& r) {
    const bool negative = r.value < 0;
    const bool zero     = r.value == 0;
    const bool sign     = negative != r.over_range;
    return {r.carry, negative, zero, r.overflow, !(sign || zero), !sign};
}

uint32_t ShDspAlu::Dsr(const Flags& f, bool dc_is_carry) const {
    const uint32_t dsr  = state_->dsr;
    const uint32_t mode = dc_is_carry ? 0u : (dsr & kDsrCs) >> kDsrCsShift;
    bool           dc   = (dsr & kDsrDc) != 0u;
    switch (static_cast<DcMode>(mode)) {
    case DcMode::kCarry: dc = f.carry; break;
    case DcMode::kNegative: dc = f.negative; break;
    case DcMode::kZero: dc = f.zero; break;
    case DcMode::kOverflow: dc = f.overflow; break;
    case DcMode::kGreater: dc = f.greater; break;
    case DcMode::kGreaterEqual: dc = f.greater_equal; break;
    }
    return (dsr & kDsrCs) | (dc ? kDsrDc : 0u) | (f.overflow ? kDsrV : 0u) | (f.negative ? kDsrN : 0u) |
           (f.zero ? kDsrZ : 0u) | (f.greater ? kDsrGt : 0u);
}

int32_t ShDspAlu::ShiftAmount(uint32_t field_b, bool immediate, uint32_t bits, int32_t limit) const {
    const uint32_t raw    = immediate ? field_b >> 4 : ShDspTopWord(Word(Sy(field_b)));
    const int32_t  amount = ShDspSignedField(raw, bits);
    if (amount < -limit || amount > limit) {
        engine_.Halt(HaltReason::kUnimplemented, "ShDspAlu: shift amount %d outside -%d..+%d, field B 0x%04X", amount,
                     limit, limit, field_b);
    }
    return amount;
}

void ShDspAlu::Arithmetic(ShDspOutcome& o, ShDspResult r, bool dc_is_carry, bool integer) const {
    r              = ShDspSaturate(r, Saturating());
    o.alu.active   = true;
    o.alu.value    = integer ? ShDspClearLowWord(r.value) : r.value;
    o.dsr_write    = true;
    o.dsr          = Dsr(ArithmeticFlags(r), dc_is_carry);
}

void ShDspAlu::Shift(ShDspOutcome& o, ShDspResult r) const {
    r               = ShDspSaturate(r, Saturating());
    Flags f         = ArithmeticFlags(r);
    f.greater       = false;
    f.greater_equal = false;
    o.alu.active    = true;
    o.alu.value     = r.value;
    o.dsr_write     = true;
    o.dsr           = Dsr(f, false);
}

void ShDspAlu::Logical(ShDspOutcome& o, uint32_t word, bool carry) const {
    o.alu.active = true;
    o.alu.value  = static_cast<int64_t>(static_cast<uint64_t>(word) << 16);
    o.dsr_write  = true;
    o.dsr        = Dsr({carry, (word & kWordSign) != 0u, word == 0u, false, false, false}, false);
}

void ShDspAlu::Execute(ShDspOutcome& o, uint32_t b, ShDspFieldB f) const {
    const ShDspReg sx  = Sx(b);
    const ShDspReg sy  = Sy(b);
    const ShDspReg dz  = static_cast<ShDspReg>(b & 0xFu);
    const ShDspReg one = f.form == ShDspForm::kSyDz ? sy : sx;
    switch (f.op) {
    case ShDspOp::kPadd:
    case ShDspOp::kPaddPmuls: Arithmetic(o, ShDspAdd(Source(sx), Source(sy), 0u), false, false); break;
    case ShDspOp::kPaddc: Arithmetic(o, ShDspAdd(Source(sx), Source(sy), DcBit()), true, false); break;
    case ShDspOp::kPsub:
    case ShDspOp::kPsubPmuls: Arithmetic(o, ShDspSub(Source(sx), Source(sy), 0u), false, false); break;
    case ShDspOp::kPsubc: Arithmetic(o, ShDspSub(Source(sx), Source(sy), DcBit()), true, false); break;
    case ShDspOp::kPcmp:
        o.dsr_write = true;
        o.dsr = Dsr(ArithmeticFlags(ShDspSaturate(ShDspSub(Source(sx), Source(sy), 0u), Saturating())), false);
        break;
    case ShDspOp::kPcopy: Arithmetic(o, ShDspAdd(Source(one), 0, 0u), false, false); break;
    case ShDspOp::kPneg: Arithmetic(o, ShDspSub(0, Source(one), 0u), false, false); break;
    case ShDspOp::kPabs: {
        const int64_t s = Source(one);
        Arithmetic(o, s < 0 ? ShDspSub(0, s, 0u) : ShDspAdd(0, s, 0u), false, false);
        break;
    }
    case ShDspOp::kPclr: Arithmetic(o, ShDspResult{0, false, false, false}, false, false); break;
    case ShDspOp::kPrnd: {
        ShDspResult r = ShDspAdd(Source(one), kShDspRound, 0u);
        r.value       = ShDspClearLowWord(r.value);
        Arithmetic(o, r, false, false);
        break;
    }
    case ShDspOp::kPinc:
        Arithmetic(o, ShDspAdd(ShDspClearLowWord(Source(one)), kShDspOneWord, 0u), false, true);
        break;
    case ShDspOp::kPdec:
        Arithmetic(o, ShDspSub(ShDspClearLowWord(Source(one)), kShDspOneWord, 0u), false, true);
        break;
    case ShDspOp::kPdmsb:
        Arithmetic(o, ShDspResult{ShDspMsbPosition(Source(one)) * kShDspOneWord, false, false, false}, false, true);
        break;
    case ShDspOp::kPand: Logical(o, ShDspTopWord(Word(sx)) & ShDspTopWord(Word(sy)), false); break;
    case ShDspOp::kPor: Logical(o, ShDspTopWord(Word(sx)) | ShDspTopWord(Word(sy)), false); break;
    case ShDspOp::kPxor: Logical(o, ShDspTopWord(Word(sx)) ^ ShDspTopWord(Word(sy)), false); break;
    case ShDspOp::kPshl:
    case ShDspOp::kPshlImm: {
        const bool            imm = f.op == ShDspOp::kPshlImm;
        const ShDspWordResult w   = ShDspShiftLogical(ShDspTopWord(Word(imm ? dz : sx)), ShiftAmount(b, imm, 6u, 16));
        Logical(o, w.word, w.carry);
        break;
    }
    case ShDspOp::kPsha:
    case ShDspOp::kPshaImm: {
        const bool imm = f.op == ShDspOp::kPshaImm;
        Shift(o, ShDspShiftArithmetic(Source(imm ? dz : sx), ShiftAmount(b, imm, 7u, 32)));
        break;
    }
    case ShDspOp::kPstsMach:
    case ShDspOp::kPstsMacl:
        o.alu.active = true;
        o.alu.value  = static_cast<int32_t>(f.op == ShDspOp::kPstsMach ? state_->mach : state_->macl);
        break;
    case ShDspOp::kPldsMach:
    case ShDspOp::kPldsMacl:
        o.mac_write = true;
        o.mac_high  = f.op == ShDspOp::kPldsMach;
        o.mac_value = Word(dz);
        break;
    case ShDspOp::kPwsb:
    case ShDspOp::kPwad:
        engine_.Halt(HaltReason::kUnimplemented, "ShDspAlu: PWSB / PWAD field B 0x%04X has no defined operation", b);
    case ShDspOp::kPmuls:
    case ShDspOp::kReserved: break;
    }
}

ShDspOutcome ShDspAlu::Evaluate(uint32_t b, ShDspFieldB f) const {
    ShDspOutcome   o{};
    const bool     multiply = f.form == ShDspForm::kMultiply || f.form == ShDspForm::kMultiplyAlu;
    const ShDspReg alu_dest =
        f.form == ShDspForm::kMultiplyAlu ? kShDspDu[b & 3u] : static_cast<ShDspReg>(b & 0xFu);
    o.alu      = {WritesAlu(f), false, alu_dest, 0};
    o.multiply = {multiply, multiply, kShDspDg[(b >> 2) & 3u], 0};
    if (multiply) {
        o.multiply.value = ShDspMultiply(Word(kShDspSe[(b >> 10) & 3u]), Word(kShDspSf[(b >> 8) & 3u]), Saturating());
        if (f.form == ShDspForm::kMultiply) return o;
    }
    const bool dc = DcBit() != 0u;
    if ((f.condition == ShDspCondition::kIfDcSet && !dc) || (f.condition == ShDspCondition::kIfDcClear && dc)) {
        return o;
    }
    Execute(o, b, f);
    if (f.condition != ShDspCondition::kAlways) o.dsr_write = false;
    return o;
}

void ShDspAlu::Apply(const ShDspOutcome& o) {
    if (o.alu.active) Write(o.alu.reg, o.alu.value);
    if (o.multiply.active) Write(o.multiply.reg, o.multiply.value);
    if (o.dsr_write) state_->dsr = o.dsr;
    if (o.mac_write) (o.mac_high ? state_->mach : state_->macl) = o.mac_value;
}

}
