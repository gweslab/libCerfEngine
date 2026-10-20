#include "thumb_stack_control_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include "arm_mark_unimplemented.h"

namespace cerfengine {

CERFENGINE_REGISTER(ThumbStackControlDecoder);

bool ThumbStackControlDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void ThumbStackControlDecoder::OnReady() {
    processor_config_ = &engine_.GetSetup().arm;
}

/* ARM DDI 0100I A7.1.9 ADD (7) (p. A7-12), A7.1.68 SUB (4) (p. A7-116). */
bool ThumbStackControlDecoder::DecodeAdjustStackPointer(DecodedInsn* insn,
                                                        uint16_t op) {
    insn->op1       = ((op >> 7) & 0x1u) != 0u ? 2u : 4u;
    insn->s         = 0u;
    insn->rs        = 0u;
    insn->rn        = 13u;
    insn->rd        = 13u;
    insn->immediate = (op & 0x7Fu) * 4u;
    insn->kind      = ArmInsnKind::kDataProcessing;
    return true;
}

bool ThumbStackControlDecoder::DecodeCompareAndBranch(DecodedInsn* insn,
                                                      uint16_t op) {
    insn->op1             = (op >> 11) & 0x1u;
    insn->rn              =  op        & 0x7u;
    insn->l               = 0u;
    insn->offset          = static_cast<int32_t>((((op >> 9) & 0x1u) << 6) |
                                                 (((op >> 3) & 0x1Fu) << 1));
    insn->r15_modified    = true;
    insn->r15_conditional = 1u;
    insn->und_in_it       = 1u;
    insn->kind            = ArmInsnKind::kCbz;
    return true;
}

/* ARM DDI 0406C.c Table A6-7 (p. A6-229): opA = bits[7:4]; opB == '0000'
   allocates NOP / YIELD / WFE / WFI / SEV, and "Other encodings in this space
   are unallocated hints. They execute as NOPs". */
bool ThumbStackControlDecoder::DecodeHint(DecodedInsn* insn, uint32_t opa) {
    if (opa == 0x3u) {
        /* A8.8.425 WFI (p. A8-1106). */
        insn->kind = ArmInsnKind::kWfi;
        return true;
    }
    insn->kind = ArmInsnKind::kNop;
    return true;
}

bool ThumbStackControlDecoder::DecodeIfThen(DecodedInsn* insn, uint16_t op) {
    const uint32_t firstcond = (op >> 4) & 0xFu;
    const uint32_t mask      =  op       & 0xFu;
    if (mask == 0u) {
        return DecodeHint(insn, firstcond);
    }
    uint32_t set_bits = 0u;
    for (uint32_t b = 0u; b < 4u; ++b) {
        set_bits += (mask >> b) & 0x1u;
    }
    if (firstcond == 0xFu || (firstcond == 0xEu && set_bits != 1u)) {
        return false;
    }
    insn->itstate       = (firstcond << 4) | mask;
    insn->itstate_valid = 1u;
    insn->kind          = ArmInsnKind::kNop;
    return true;
}

bool ThumbStackControlDecoder::DecodeChangeProcessorState(DecodedInsn* insn,
                                                          uint16_t op) {
    const uint32_t disable = (op >> 4) & 0x1u;
    const uint32_t aif     =  op       & 0x7u;
    if (aif == 0u) {
        return false;
    }

    const uint32_t aif_mask = aif << 6;
    insn->op1          = aif_mask;
    insn->immediate    = disable != 0u ? aif_mask : 0u;
    insn->und_in_it    = 1u;
    /* QEMU ARM translate.c trans_CPS -> gen_set_psr_im ->
       gen_lookup_tb (DISAS_EXIT). */
    insn->context_sync = true;
    insn->kind         = ArmInsnKind::kCps;
    return true;
}

bool ThumbStackControlDecoder::DecodeExtend(DecodedInsn* insn, uint16_t op) {
    insn->rm  = (op >> 3) & 0x7u;
    insn->rd  =  op       & 0x7u;
    insn->op1 = 0u;
    switch ((op >> 6) & 0x3u) {
    case 0x0u: insn->kind = ArmInsnKind::kSxth; break;
    case 0x1u: insn->kind = ArmInsnKind::kSxtb; break;
    case 0x2u: insn->kind = ArmInsnKind::kUxth; break;
    default:   insn->kind = ArmInsnKind::kUxtb; break;
    }
    return true;
}

bool ThumbStackControlDecoder::DecodeByteReverse(DecodedInsn* insn,
                                                 uint16_t op) {
    insn->rm = (op >> 3) & 0x7u;
    insn->rd =  op       & 0x7u;
    switch ((op >> 6) & 0x3u) {
    case 0x0u: insn->kind = ArmInsnKind::kRev;   break;
    case 0x1u: insn->kind = ArmInsnKind::kRev16; break;
    case 0x3u: insn->kind = ArmInsnKind::kRevsh; break;
    default:   return false;
    }
    return true;
}

/* ARM DDI 0100I Figure A6-2 (A6.2.1, p. A6-5), bits[15:12] == 0b1011, and its
   closing note: "Any instruction with bits[15:12] = 1011, and which is not
   shown in Figure A6-2, is an Undefined instruction." */
bool ThumbStackControlDecoder::DecodeStackControlGroup(DecodedInsn* insn,
                                                       uint16_t op) {
    switch ((op >> 8) & 0xFu) {
    case 0x0u:
        return DecodeAdjustStackPointer(insn, op);
    case 0x2u:
        /* Figure A6-2 note 2, p. A6-5. */
        if (!processor_config_->extend_rotate) return false;
        return DecodeExtend(insn, op);
    case 0x4u:
    case 0x5u:
    case 0xCu:
    case 0xDu: {
        /* A7.1.50 PUSH, p. A7-85 (SP decrement-before, R adds LR); A7.1.49
           POP, p. A7-82 (SP increment-after, R adds PC). Both are
           UNPREDICTABLE when bits[8:0] are zero. */
        if ((op & 0x1FFu) == 0u) return false;
        const bool     load = ((op >> 11) & 0x1u) != 0u;
        const uint32_t r    = (op >> 8) & 0x1u;
        insn->register_list = static_cast<uint16_t>(
            (op & 0xFFu) | (r << (load ? 15u : 14u)));
        insn->rn           = 13u;
        insn->l            = load ? 1u : 0u;
        insn->p            = load ? 0u : 1u;
        insn->u            = load ? 1u : 0u;
        insn->w            = 1u;
        insn->s            = 0u;
        insn->r15_modified = load && r != 0u;
        insn->kind         = ArmInsnKind::kBlockDataTransfer;
        return true;
    }
    case 0x6u: {
        const uint32_t lo = op & 0xFFu;
        /* Set Endianness: bits[7:4] == 0b0101. Change Processor State:
           bits[7:5] == 0b011 with bit[3] == 0. Figure A6-2 note 2, p. A6-5. */
        const bool setend = (lo & 0xF0u) == 0x50u;
        const bool cps    = (lo & 0xE0u) == 0x60u && (lo & 0x08u) == 0u;
        if (!setend && !cps) return false;
        if (!processor_config_->arch_v6) return false;
        if (setend) return MarkArmUnimplemented(insn, op);
        return DecodeChangeProcessorState(insn, op);
    }
    case 0x1u:
    case 0x3u:
    case 0x9u:
    case 0xBu:
        /* ARM DDI 0406C.c Table A6-6 (A6.2.5, p. A6-228) allocates opcode
           0001xxx/0011xxx/1001xxx/1011xxx to CBNZ, CBZ, variant v6T2. */
        if (!processor_config_->thumb2) return false;
        return DecodeCompareAndBranch(insn, op);
    case 0xFu:
        if (!processor_config_->thumb2) return false;
        return DecodeIfThen(insn, op);
    case 0xAu:
        /* Figure A6-2 note 2, p. A6-5. */
        if (!processor_config_->rev) return false;
        return DecodeByteReverse(insn, op);
    case 0xEu:
        /* Figure A6-2 note 1, p. A6-5. DDI 0406C.c A8.8.24 BKPT encoding T1
           (p. A8-346): "Breakpoint is always unconditional, even when inside
           an IT block." */
        if (!processor_config_->blx_reg) return false;
        insn->uncond_in_it = 1u;
        return MarkArmUnimplemented(insn, op);
    default:
        return false;
    }
}

}
