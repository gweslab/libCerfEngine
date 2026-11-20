#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/engine_setup.h>

#include "../arm_emit_services.h"
#include "../arm_unimplemented_halt.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

constexpr uint32_t kWcid  = 0u;
constexpr uint32_t kWcon  = 1u;
constexpr uint32_t kWcssf = 2u;
constexpr uint32_t kWcasf = 3u;
constexpr uint32_t kWcgr0 = 8u;
constexpr uint32_t kWcgr3 = 11u;

constexpr uint32_t kWconCup = 1u << 0;
constexpr uint32_t kWconMup = 1u << 1;

constexpr uint32_t kAccumulatorHighMask = 0xFFu;

constexpr uint32_t kMia   = 0x0u;
constexpr uint32_t kMiaph = 0x8u;
constexpr uint32_t kMiaxy = 0xCu;

uint32_t Cp0Disp(uint32_t index, bool high) {
    return static_cast<uint32_t>(offsetof(ArmCpuState, xscale_cp0) + index * 8u + (high ? 4u : 0u));
}

uint32_t Cp1Disp(uint32_t index) {
    return static_cast<uint32_t>(offsetof(ArmCpuState, xscale_cp1) + index * 4u);
}

Ref Word(ArmIrContext& c, uint32_t disp) { return c.b->GetSlot(Type::kI32, disp); }

void SetWord(ArmIrContext& c, uint32_t disp, Ref value) { c.b->SetSlot(disp, value); }

void OrWcon(ArmIrContext& c, uint32_t bits) {
    SetWord(c, Cp1Disp(kWcon), c.b->Op2(Op::kOr, Word(c, Cp1Disp(kWcon)), c.b->Const32(bits)));
}

void Halt(ArmIrContext& c) {
    ArmIrUnimplementedHalt(c, reinterpret_cast<const void*>(&ArmUnimplementedHalt::InstructionHelper),
                           c.d->raw);
}

bool Gate(ArmIrContext& c) {
    if (ArmIrCoprocessorPermissionCheck(c, true)) return true;
    ArmIrCoprocGate(c, 1u << c.d->cp_num);
    return false;
}

uint32_t DoubleOpcode(const DecodedInsn& d) { return (static_cast<uint32_t>(d.offset) >> 4) & 0xFu; }

uint32_t DoubleCrm(const DecodedInsn& d) { return static_cast<uint32_t>(d.offset) & 0xFu; }

bool UsesPc(const DecodedInsn& d) { return d.crd == ArmGpr::kR15 || d.rn == ArmGpr::kR15; }

void IwmmxtControlWrite(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    cerfir::Builder&   b = *c.b;
    switch (d.crn) {
    case kWcid:
    case kWcasf:
        return;
    case kWcon: {
        const Ref set = b.Op2(Op::kOr, Word(c, Cp1Disp(kWcon)), b.Const32(kWconCup));
        SetWord(c, Cp1Disp(kWcon), b.Op2(Op::kAndNot, set, ArmIrReadGpr(c, d.rd)));
        return;
    }
    case kWcssf:
        SetWord(c, Cp1Disp(kWcssf), b.Op2(Op::kAndNot, Word(c, Cp1Disp(kWcssf)), ArmIrReadGpr(c, d.rd)));
        return;
    default:
        break;
    }
    if (d.crn < kWcgr0 || d.crn > kWcgr3) {
        ArmIrUndefined(c);
        return;
    }
    OrWcon(c, kWconCup);
    SetWord(c, Cp1Disp(d.crn), ArmIrReadGpr(c, d.rd));
}

bool IwmmxtControlTransfer(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    if (d.cp_num != 1u || d.cp_opc != 0u || d.cp != 0u) return false;
    if (d.l != 0u && d.rd == ArmGpr::kR15) return false;
    if (Gate(c)) return true;
    if (d.crm != 0u) {
        ArmIrUndefined(c);
        return true;
    }
    if (d.l == 0u) {
        IwmmxtControlWrite(c);
        return true;
    }
    const Ref value = d.crn == kWcid ? c.b->Const32(c.block->emit->ProcessorConfig()->wcid)
                                     : Word(c, Cp1Disp(d.crn));
    ArmIrWriteGpr(c, d.rd, value);
    return true;
}

void Accumulate40(ArmIrContext& c, Ref product) {
    cerfir::Builder& b   = *c.b;
    const Ref        acc = b.Op2(Op::kPair, Word(c, Cp0Disp(0u, false)), Word(c, Cp0Disp(0u, true)));
    const Ref        sum = b.Op2(Op::kAdd, acc, product);
    SetWord(c, Cp0Disp(0u, false), b.Op1(Op::kLo32, sum));
    SetWord(c, Cp0Disp(0u, true), b.Op2(Op::kAnd, b.Op1(Op::kHi32, sum), b.Const32(kAccumulatorHighMask)));
}

Ref HalfProduct(ArmIrContext& c, bool rm_top, bool rs_top) {
    const DecodedInsn& d = *c.d;
    return c.b->Op2(Op::kMulS64, ArmIrSignedHalf(c, ArmIrReadGpr(c, d.crm), rm_top),
                    ArmIrSignedHalf(c, ArmIrReadGpr(c, d.rd), rs_top));
}

bool DspMultiplyAccumulate(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    if (d.cp_num != 0u || d.l != 0u || d.cp_opc != 1u) return false;
    if (Gate(c)) return true;
    const uint32_t op    = d.crn;
    const bool     known = op == kMia || op == kMiaph || op >= kMiaxy;
    if (!known || d.cp != 0u || d.rd == ArmGpr::kR15 || d.crm == ArmGpr::kR15) {
        Halt(c);
        return true;
    }
    cerfir::Builder& b = *c.b;
    Ref product = cerfir::kNoRef;
    if (op == kMia) {
        product = b.Op2(Op::kMulS64, ArmIrReadGpr(c, d.crm), ArmIrReadGpr(c, d.rd));
    } else if (op == kMiaph) {
        product = b.Op2(Op::kAdd, HalfProduct(c, true, true), HalfProduct(c, false, false));
    } else {
        product = HalfProduct(c, (op & 2u) != 0u, (op & 1u) != 0u);
    }
    Accumulate40(c, product);
    return true;
}

bool IwmmxtRegisterPair(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    if (d.cp_num > 1u) return false;
    if (Gate(c)) return true;
    if (d.cp_num != 0u || DoubleOpcode(d) != 0u || UsesPc(d)) {
        Halt(c);
        return true;
    }
    const uint32_t wr = DoubleCrm(d);
    if (d.l != 0u) {
        ArmIrWriteGpr(c, d.crd, Word(c, Cp0Disp(wr, false)));
        ArmIrWriteGpr(c, d.rn, Word(c, Cp0Disp(wr, true)));
        return true;
    }
    SetWord(c, Cp0Disp(wr, false), ArmIrReadGpr(c, d.crd));
    SetWord(c, Cp0Disp(wr, true), ArmIrReadGpr(c, d.rn));
    OrWcon(c, kWconMup);
    return true;
}

bool DspAccumulatorAccess(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    if (d.cp_num != 0u) return false;
    if (Gate(c)) return true;
    if (DoubleOpcode(d) != 0u || DoubleCrm(d) != 0u || UsesPc(d)) {
        Halt(c);
        return true;
    }
    cerfir::Builder& b = *c.b;
    if (d.l != 0u) {
        ArmIrWriteGpr(c, d.crd, Word(c, Cp0Disp(0u, false)));
        const Ref high = b.Convert(Op::kTruncate, Type::kI8, Word(c, Cp0Disp(0u, true)));
        ArmIrWriteGpr(c, d.rn, b.Convert(Op::kSignExtend, Type::kI32, high));
        return true;
    }
    SetWord(c, Cp0Disp(0u, false), ArmIrReadGpr(c, d.crd));
    SetWord(c, Cp0Disp(0u, true), b.Op2(Op::kAnd, ArmIrReadGpr(c, d.rn), b.Const32(kAccumulatorHighMask)));
    return true;
}

}

bool ArmIrXscaleRegisterTransfer(ArmIrContext& c) {
    const ArmFeatures& f = *c.block->emit->ProcessorConfig();
    if (f.iwmmxt) return IwmmxtControlTransfer(c);
    if (f.xscale_accumulator) return DspMultiplyAccumulate(c);
    return false;
}

bool ArmIrXscaleRegisterTransferDouble(ArmIrContext& c) {
    const ArmFeatures& f = *c.block->emit->ProcessorConfig();
    if (f.iwmmxt) return IwmmxtRegisterPair(c);
    if (f.xscale_accumulator) return DspAccumulatorAccess(c);
    return false;
}

}
