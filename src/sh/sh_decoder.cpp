#include "sh_decoder.h"

#include <cerfengine/sh/sh_cpu_state.h>

namespace cerfengine {

CERFENGINE_REGISTER(ShDecoder);

namespace {

constexpr bool IsSizedMove(uint32_t low4, uint32_t group) {
    return (low4 & 0xCu) == group && (low4 & 3u) != 3u;
}

constexpr uint8_t SizeOf(uint32_t low4) {
    return static_cast<uint8_t>(1u << (low4 & 3u));
}

constexpr uint32_t SignExtend8(uint32_t value) {
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(value & 0xFFu)));
}

constexpr uint32_t SignExtend12(uint32_t value) {
    return (value & 0x800u) != 0u ? (value | 0xFFFFF000u) : (value & 0xFFFu);
}

constexpr uint32_t BranchTarget(uint32_t guest_address, uint32_t displacement) {
    return ShPcValue(guest_address) + (displacement << 1);
}

void SetDisplacementMove(ShDecodedInsn& d, ShInsnKind kind, uint32_t n, uint32_t m, uint8_t width,
                         uint32_t scaled) {
    d.kind  = kind;
    d.n     = static_cast<uint8_t>(n);
    d.m     = static_cast<uint8_t>(m);
    d.width = width;
    d.disp  = scaled * width;
}

void SetJump(ShDecodedInsn& d, ShJumpTarget target, bool link) {
    d.kind           = ShInsnKind::kJump;
    d.jump_target    = target;
    d.m              = d.n;
    d.link           = link ? 1u : 0u;
    d.cycles         = 2u;
    d.delayed_branch = 1u;
    d.slot_illegal   = 1u;
}

void SetBranchAlways(ShDecodedInsn& d, bool link) {
    d.kind           = ShInsnKind::kBranchAlways;
    d.disp           = BranchTarget(d.guest_address, SignExtend12(d.raw));
    d.link           = link ? 1u : 0u;
    d.cycles         = 2u;
    d.delayed_branch = 1u;
    d.slot_illegal   = 1u;
}

void SetMultiply(ShDecodedInsn& d, ShMultiply multiply, uint8_t cycles) {
    d.kind     = ShInsnKind::kMultiply;
    d.multiply = multiply;
    d.cycles   = cycles;
}

void SetCompare(ShDecodedInsn& d, ShCmp cmp) {
    d.kind = ShInsnKind::kCmp;
    d.cmp  = cmp;
}

constexpr ShLogic kLogicBySelector[4] = {ShLogic::kTest, ShLogic::kAnd, ShLogic::kXor, ShLogic::kOr};

void SetLogic(ShDecodedInsn& d, ShInsnKind kind, uint32_t selector) {
    d.kind  = kind;
    d.logic = kLogicBySelector[selector & 3u];
}

constexpr ShArith kExtendByLow2[4] = {ShArith::kExtuB, ShArith::kExtuW, ShArith::kExtsB, ShArith::kExtsW};

void SetArith(ShDecodedInsn& d, ShArith arith) {
    d.kind  = ShInsnKind::kArith;
    d.arith = arith;
}

void SetShift(ShDecodedInsn& d, ShShift shift) {
    d.kind  = ShInsnKind::kShift;
    d.shift = shift;
}

struct ShiftCode {
    uint8_t low_byte;
    ShShift shift;
};

constexpr ShiftCode kShiftByLowByte[] = {
    {0x00u, ShShift::kShll},   {0x01u, ShShift::kShlr},   {0x04u, ShShift::kRotl},  {0x05u, ShShift::kRotr},
    {0x08u, ShShift::kShll2},  {0x09u, ShShift::kShlr2},  {0x18u, ShShift::kShll8}, {0x19u, ShShift::kShlr8},
    {0x20u, ShShift::kShal},   {0x21u, ShShift::kShar},   {0x24u, ShShift::kRotcl}, {0x25u, ShShift::kRotcr},
    {0x28u, ShShift::kShll16}, {0x29u, ShShift::kShlr16},
};

void SetSingleRegisterShift(ShDecodedInsn& d) {
    for (const ShiftCode& code : kShiftByLowByte) {
        if ((d.raw & 0x00FFu) == code.low_byte) SetShift(d, code.shift);
    }
}

void SetBranch(ShDecodedInsn& d, bool delayed, bool if_t) {
    d.kind           = delayed ? ShInsnKind::kBranchDelayed : ShInsnKind::kBranch;
    d.disp           = BranchTarget(d.guest_address, SignExtend8(d.raw));
    d.branch_if_t    = if_t ? 1u : 0u;
    d.slot_illegal   = 1u;
    d.delayed_branch = delayed ? 1u : 0u;
    d.ends_block     = delayed ? 0u : 1u;
}

}

void ShDecoder::OnReady() {
    dsp_ = engine_.GetSetup().sh.dsp;
}

void ShDecoder::SetSystemTransfer(ShDecodedInsn& d, ShInsnKind kind) const {
    const uint32_t which = (d.raw >> 4) & 0xFu;
    const bool     base  = which <= static_cast<uint32_t>(ShSystemRegister::kPr);
    const bool     dsp   = dsp_ && which >= static_cast<uint32_t>(ShSystemRegister::kDsr) &&
                     which <= static_cast<uint32_t>(ShSystemRegister::kY1);
    if (!base && !dsp) return;
    d.kind            = kind;
    d.system_register = static_cast<ShSystemRegister>(which);
    d.dsp_mode        = dsp ? 1u : 0u;
    d.m               = d.n;
    d.width           = 4u;
}

uint8_t ShDecoder::ControlCycles(ShInsnKind kind, const ShControlClass& r) const {
    if (kind == ShInsnKind::kControlStore) return 1u;
    if (kind == ShInsnKind::kControlStorePreDec) return (r.dsp_unit || r.bank || dsp_) ? 2u : 1u;
    if (kind == ShInsnKind::kControlLoad) return r.sr ? 5u : (r.dsp_unit || dsp_) ? 3u : 1u;
    return r.sr ? 7u : (r.dsp_unit || dsp_) ? 5u : 1u;
}

void ShDecoder::SetControlTransfer(ShDecodedInsn& d, ShInsnKind kind) const {
    const uint32_t       which = (d.raw >> 4) & 0xFu;
    const ShControlClass r{
        which == static_cast<uint32_t>(ShControlRegister::kSr),
        which >= static_cast<uint32_t>(ShControlRegister::kMod) && which <= static_cast<uint32_t>(ShControlRegister::kRe),
        which >= static_cast<uint32_t>(ShControlRegister::kBank0),
    };
    if (r.dsp_unit && !dsp_) return;
    d.kind             = kind;
    d.control_register = static_cast<uint8_t>(which);
    d.dsp_mode         = r.dsp_unit ? 1u : 0u;
    d.privileged       = which != static_cast<uint32_t>(ShControlRegister::kGbr) ? 1u : 0u;
    d.m                = d.n;
    d.width            = 4u;
    d.cycles           = ControlCycles(kind, r);
    const bool loads   = kind == ShInsnKind::kControlLoad || kind == ShInsnKind::kControlLoadPostInc;
    if (loads && r.sr) {
        d.slot_illegal = 1u;
        d.ends_block   = 1u;
    }
}

ShDecodedInsn ShDecoder::Decode(uint16_t raw, uint32_t guest_address) const {
    ShDecodedInsn d{};
    d.raw           = raw;
    d.guest_address = guest_address;
    d.n             = static_cast<uint8_t>((raw >> 8) & 0xFu);
    d.m             = static_cast<uint8_t>((raw >> 4) & 0xFu);
    d.kind          = ShInsnKind::kUnimplemented;
    d.cycles        = 1u;

    const uint32_t low4 = raw & 0xFu;
    switch (raw >> 12) {
    case 0x0u:
        if (raw == 0x0009u) {
            d.kind = ShInsnKind::kNop;
        } else if (raw == 0x000Bu) {
            SetJump(d, ShJumpTarget::kProcedureRegister, false);
        } else if (raw == 0x002Bu) {
            d.kind           = ShInsnKind::kRte;
            d.cycles         = 4u;
            d.delayed_branch = 1u;
            d.slot_illegal   = 1u;
            d.privileged     = 1u;
        } else if (raw == 0x0038u) {
            d.kind       = ShInsnKind::kLdtlb;
            d.privileged = 1u;
        } else if (raw == 0x001Bu) {
            d.kind       = ShInsnKind::kSleep;
            d.cycles     = 4u;
            d.privileged = 1u;
            d.ends_block = 1u;
        } else if ((raw & 0x00FFu) == 0x0023u) {
            SetJump(d, ShJumpTarget::kPcPlusRegister, false);
        } else if ((raw & 0x00FFu) == 0x0003u) {
            SetJump(d, ShJumpTarget::kPcPlusRegister, true);
        } else if (raw == 0x0019u) {
            SetArith(d, ShArith::kDiv0u);
        } else if ((raw & 0x00FFu) == 0x0029u) {
            d.kind = ShInsnKind::kMovt;
        } else if ((raw & 0x00FFu) == 0x0083u) {
            d.kind   = ShInsnKind::kNop;
            d.cycles = dsp_ ? 2u : 1u;
        } else if (raw == 0x0028u) {
            d.kind = ShInsnKind::kClrmac;
        } else if (raw == 0x0008u || raw == 0x0048u) {
            d.kind = ShInsnKind::kClearSrBits;
            d.disp = raw == 0x0008u ? kShSrT : kShSrS;
        } else if (raw == 0x0018u || raw == 0x0058u) {
            d.kind = ShInsnKind::kSetSrBits;
            d.disp = raw == 0x0018u ? kShSrT : kShSrS;
        } else if (low4 == 0x7u) {
            SetMultiply(d, ShMultiply::kMulL, 2u);
        } else if (low4 == 0xAu) {
            SetSystemTransfer(d, ShInsnKind::kSystemStore);
        } else if (low4 == 0x2u) {
            SetControlTransfer(d, ShInsnKind::kControlStore);
        } else if (IsSizedMove(low4, 0x4u)) {
            d.kind  = ShInsnKind::kMovStoreIndexed;
            d.width = SizeOf(low4);
        } else if (IsSizedMove(low4, 0xCu)) {
            d.kind  = ShInsnKind::kMovLoadIndexed;
            d.width = SizeOf(low4);
        }
        break;
    case 0x2u:
        if (IsSizedMove(low4, 0x0u)) {
            d.kind  = ShInsnKind::kMovStore;
            d.width = SizeOf(low4);
        } else if (IsSizedMove(low4, 0x4u)) {
            d.kind  = ShInsnKind::kMovStorePreDec;
            d.width = SizeOf(low4);
        } else if (low4 == 0x7u) {
            SetArith(d, ShArith::kDiv0s);
        } else if (low4 == 0xCu) {
            SetCompare(d, ShCmp::kStr);
        } else if (low4 == 0xDu) {
            d.kind = ShInsnKind::kXtrct;
        } else if (low4 == 0xEu) {
            SetMultiply(d, ShMultiply::kMuluW, 1u);
        } else if (low4 == 0xFu) {
            SetMultiply(d, ShMultiply::kMulsW, 1u);
        } else if ((low4 & 0xCu) == 0x8u) {
            SetLogic(d, ShInsnKind::kLogic, low4);
        }
        break;
    case 0x3u:
        if (low4 == 0x0u) SetCompare(d, ShCmp::kEq);
        else if (low4 == 0x2u) SetCompare(d, ShCmp::kHs);
        else if (low4 == 0x3u) SetCompare(d, ShCmp::kGe);
        else if (low4 == 0x4u) SetArith(d, ShArith::kDiv1);
        else if (low4 == 0x5u) SetMultiply(d, ShMultiply::kDmuluL, 2u);
        else if (low4 == 0x6u) SetCompare(d, ShCmp::kHi);
        else if (low4 == 0x7u) SetCompare(d, ShCmp::kGt);
        else if (low4 == 0x8u) SetArith(d, ShArith::kSub);
        else if (low4 == 0xAu) SetArith(d, ShArith::kSubc);
        else if (low4 == 0xBu) SetArith(d, ShArith::kSubv);
        else if (low4 == 0xCu) SetArith(d, ShArith::kAdd);
        else if (low4 == 0xDu) SetMultiply(d, ShMultiply::kDmulsL, 2u);
        else if (low4 == 0xEu) SetArith(d, ShArith::kAddc);
        else if (low4 == 0xFu) SetArith(d, ShArith::kAddv);
        break;
    case 0x4u:
        if ((raw & 0x00FFu) == 0x0015u) {
            SetCompare(d, ShCmp::kPl);
        } else if ((raw & 0x00FFu) == 0x0011u) {
            SetCompare(d, ShCmp::kPz);
        } else if ((raw & 0x00FFu) == 0x0010u) {
            SetArith(d, ShArith::kDt);
        } else if ((raw & 0x00FFu) == 0x002Bu) {
            SetJump(d, ShJumpTarget::kRegister, false);
        } else if ((raw & 0x00FFu) == 0x000Bu) {
            SetJump(d, ShJumpTarget::kRegister, true);
        } else if ((raw & 0x00FFu) == 0x001Bu) {
            d.kind   = ShInsnKind::kTas;
            d.width  = 1u;
            d.cycles = dsp_ ? 4u : 3u;
        } else if (low4 == 0xAu) {
            SetSystemTransfer(d, ShInsnKind::kSystemLoad);
        } else if (low4 == 0x6u) {
            SetSystemTransfer(d, ShInsnKind::kSystemLoadPostInc);
        } else if (low4 == 0x2u) {
            SetSystemTransfer(d, ShInsnKind::kSystemStorePreDec);
        } else if (low4 == 0xEu) {
            SetControlTransfer(d, ShInsnKind::kControlLoad);
        } else if (low4 == 0x7u) {
            SetControlTransfer(d, ShInsnKind::kControlLoadPostInc);
        } else if (low4 == 0x3u) {
            SetControlTransfer(d, ShInsnKind::kControlStorePreDec);
        } else if (low4 == 0xCu) {
            SetShift(d, ShShift::kShad);
        } else if (low4 == 0xDu) {
            SetShift(d, ShShift::kShld);
        } else {
            SetSingleRegisterShift(d);
        }
        break;
    case 0x1u:
        SetDisplacementMove(d, ShInsnKind::kMovStoreDisp, d.n, d.m, 4u, raw & 0xFu);
        break;
    case 0x5u:
        SetDisplacementMove(d, ShInsnKind::kMovLoadDisp, d.n, d.m, 4u, raw & 0xFu);
        break;
    case 0x6u:
        if (low4 == 0x8u || low4 == 0x9u) {
            d.kind  = ShInsnKind::kSwap;
            d.width = low4 == 0x8u ? 1u : 2u;
        } else if (low4 == 0x3u) {
            d.kind = ShInsnKind::kMov;
        } else if (low4 == 0x7u) {
            d.kind = ShInsnKind::kNot;
        } else if (low4 == 0xAu) {
            SetArith(d, ShArith::kNegc);
        } else if (low4 == 0xBu) {
            SetArith(d, ShArith::kNeg);
        } else if (low4 >= 0xCu) {
            SetArith(d, kExtendByLow2[low4 & 3u]);
        } else if (IsSizedMove(low4, 0x0u)) {
            d.kind  = ShInsnKind::kMovLoad;
            d.width = SizeOf(low4);
        } else if (IsSizedMove(low4, 0x4u)) {
            d.kind  = ShInsnKind::kMovLoadPostInc;
            d.width = SizeOf(low4);
        }
        break;
    case 0x7u:
        d.kind = ShInsnKind::kAddImm;
        d.disp = SignExtend8(raw);
        break;
    case 0x8u:
        switch (d.n) {
        case 0x0u:
        case 0x1u:
            SetDisplacementMove(d, ShInsnKind::kMovStoreDisp, d.m, 0u, SizeOf(d.n), raw & 0xFu);
            break;
        case 0x4u:
        case 0x5u:
            SetDisplacementMove(d, ShInsnKind::kMovLoadDisp, 0u, d.m, SizeOf(d.n), raw & 0xFu);
            break;
        case 0x8u:
            SetCompare(d, ShCmp::kEqImm);
            d.disp = SignExtend8(raw);
            break;
        case 0x9u: SetBranch(d, false, true); break;
        case 0xBu: SetBranch(d, false, false); break;
        case 0xDu: SetBranch(d, true, true); break;
        case 0xFu: SetBranch(d, true, false); break;
        default: break;
        }
        break;
    case 0xAu:
        SetBranchAlways(d, false);
        break;
    case 0xBu:
        SetBranchAlways(d, true);
        break;
    case 0x9u:
        d.kind  = ShInsnKind::kMovPcRel;
        d.width = 2u;
        d.disp  = (raw & 0x00FFu) << 1;
        break;
    case 0xCu:
        if (d.n <= 0x2u) {
            SetDisplacementMove(d, ShInsnKind::kMovStoreGbr, 0u, 0u, SizeOf(d.n), raw & 0xFFu);
        } else if (d.n >= 0x4u && d.n <= 0x6u) {
            SetDisplacementMove(d, ShInsnKind::kMovLoadGbr, 0u, 0u, SizeOf(d.n), raw & 0xFFu);
        } else if (d.n == 0x7u) {
            d.kind = ShInsnKind::kMova;
            d.disp = (raw & 0xFFu) << 2;
        } else if ((d.n & 0xCu) == 0x8u) {
            SetLogic(d, ShInsnKind::kLogicImm, d.n);
            d.disp = raw & 0x00FFu;
        } else if ((d.n & 0xCu) == 0xCu) {
            SetLogic(d, ShInsnKind::kLogicGbr, d.n);
            d.disp   = raw & 0x00FFu;
            d.width  = 1u;
            d.cycles = 3u;
        }
        break;
    case 0xDu:
        d.kind  = ShInsnKind::kMovPcRel;
        d.width = 4u;
        d.disp  = (raw & 0x00FFu) << 2;
        break;
    case 0xEu:
        d.kind = ShInsnKind::kMovImm;
        d.disp = SignExtend8(raw);
        break;
    case 0xFu:
        if (!dsp_) {
            d.kind         = ShInsnKind::kReserved;
            d.slot_illegal = 1u;
            d.ends_block   = 1u;
        }
        break;
    default:
        break;
    }
    return d;
}

}
