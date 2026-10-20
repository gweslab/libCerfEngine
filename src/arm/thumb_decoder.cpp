#include "thumb_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include "arm_mark_unimplemented.h"
#include "thumb_stack_control_decoder.h"
#include "thumb_transfer_decoder.h"

namespace cerfengine {

CERFENGINE_REGISTER(ThumbDecoder);

bool ThumbDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void ThumbDecoder::OnReady() {
    processor_config_ = &engine_.GetSetup().arm;
    transfer_decoder_ = &engine_.Get<ThumbTransferDecoder>();
    stack_control_    = &engine_.Get<ThumbStackControlDecoder>();
}

/* ARM DDI 0100I A7.1.38 LSL (1) (p. A7-64), A7.1.40 LSR (1) (p. A7-68),
   A7.1.11 ASR (1) (p. A7-15). */
bool ThumbDecoder::DecodeShiftByImmediate(DecodedInsn* insn, uint16_t op) {
    const uint32_t shift_t = (op >> 11) & 0x3u;
    const uint32_t immed_5 = (op >>  6) & 0x1Fu;
    insn->op1      = 13u;
    insn->s        = 1u;
    /* DDI 0406C.c A8.8.103 MOV (register) T2 (p. A8-486): "setflags = TRUE"
       and "Not permitted in IT block", where A8.8.94 LSL (immediate)
       (p. A8-468) carries "setflags = !InITBlock()". */
    const bool mov_reg_t2 = shift_t == kSrLsl && immed_5 == 0u;
    insn->s_outside_it = mov_reg_t2 ? 0u : 1u;
    insn->und_in_it    = mov_reg_t2 ? 1u : 0u;
    insn->rn       = 0u;
    insn->rd       =  op       & 0x7u;
    insn->rm       = (op >> 3) & 0x7u;
    insn->n        = shift_t;
    insn->rs       =
        (shift_t != kSrLsl && immed_5 == 0u) ? 32u : immed_5;
    insn->kind = ArmInsnKind::kDataProcessingReg;
    return true;
}

/* ARM DDI 0100I A7.1.5 (A7-7), A7.1.3 (A7-5), A7.1.67 (A7-115), A7.1.65
   (A7-113). */
bool ThumbDecoder::DecodeAddSubtract(DecodedInsn* insn, uint16_t op) {
    insn->op1 = ((op >> 9) & 0x1u) != 0u ? 2u : 4u;
    insn->s   = 1u;
    insn->s_outside_it = 1u;
    insn->rn  = (op >> 3) & 0x7u;
    insn->rd  =  op       & 0x7u;
    insn->rs  = 0u;
    if (((op >> 10) & 0x1u) != 0u) {
        insn->immediate = (op >> 6) & 0x7u;
        insn->kind      = ArmInsnKind::kDataProcessing;
        return true;
    }
    insn->rm       = (op >> 6) & 0x7u;
    insn->n        = kSrLsl;
    insn->kind     = ArmInsnKind::kDataProcessingReg;
    return true;
}

/* ARM DDI 0100I A7.1.42 (A7-72), A7.1.21 (A7-35), A7.1.4 (A7-6), A7.1.66
   (A7-114). */
bool ThumbDecoder::DecodeImmediateOperations(DecodedInsn* insn, uint16_t op) {
    const uint32_t reg = (op >> 8) & 0x7u;
    insn->s         = 1u;
    insn->s_outside_it = 1u;
    insn->rs        = 0u;
    insn->immediate = op & 0xFFu;
    switch ((op >> 11) & 0x3u) {
    case 0u:
        insn->op1 = 13u;
        insn->rd  = reg;
        break;
    case 1u:
        /* DDI 0406C.c A8.8.37 CMP (immediate) (p. A8-370): "It updates the
           condition flags based on the result, and discards the result" - T1
           carries no setflags assignment. */
        insn->op1 = 10u;
        insn->rn  = reg;
        insn->s_outside_it = 0u;
        break;
    case 2u:
        insn->op1 = 4u;
        insn->rd  = reg;
        insn->rn  = reg;
        break;
    default:
        insn->op1 = 2u;
        insn->rd  = reg;
        insn->rn  = reg;
        break;
    }
    insn->kind = ArmInsnKind::kDataProcessing;
    return true;
}

/* ARM DDI 0100I A7.1.6 ADD (4) (p. A7-8), A7.1.23 CMP (3) (p. A7-37),
   A7.1.44 MOV (3) (p. A7-75), A7.1.25 CPY (p. A7-41). */
bool ThumbDecoder::DecodeSpecialDataProcessing(DecodedInsn* insn,
                                               uint16_t     op) {
    const uint32_t h1  = (op >> 7) & 0x1u;
    const uint32_t h2  = (op >> 6) & 0x1u;
    const bool     low = h1 == 0u && h2 == 0u;
    const uint32_t reg = (h1 << 3) | (op & 0x7u);
    insn->rm = (h2 << 3) | ((op >> 3) & 0x7u);
    insn->n  = kSrLsl;
    insn->rs = 0u;
    switch ((op >> 8) & 0x3u) {
    case 0u:
        /* ARM DDI 0406C.c Table A6-4 (A6.2.3, p. A6-226): opcode 0000 is Add Low
           Registers, variant v6T2, footnote a "UNPREDICTABLE in earlier
           variants". */
        if (low && !processor_config_->thumb2) return false;
        /* ARM DDI 0406C.c A8.8.6 ADD (register, Thumb) encoding T2
           (p. A8-310): "if n == 15 && m == 15 then UNPREDICTABLE". */
        if (reg == 15u && insn->rm == 15u) return false;
        insn->op1 = 4u;
        insn->s   = 0u;
        insn->rn  = reg;
        insn->rd  = reg;
        break;
    case 1u:
        /* ARM DDI 0406C.c A8.8.38 CMP (register) encoding T2 (p. A8-372): "if
           n < 8 && m < 8 then UNPREDICTABLE; if n == 15 || m == 15 then
           UNPREDICTABLE". */
        if (low || reg == 15u || insn->rm == 15u) return false;
        insn->op1 = 10u;
        insn->s   = 1u;
        insn->rn  = reg;
        break;
    case 2u:
        if (low && !processor_config_->arch_v6) return false;
        insn->op1 = 13u;
        insn->s   = 0u;
        insn->rd  = reg;
        break;
    default:
        return MarkArmUnimplemented(insn, op);
    }
    insn->kind = ArmInsnKind::kDataProcessingReg;
    return true;
}

/* ARM DDI 0100I A7.1.19 BX (p. A7-32), A7.1.18 BLX (2) (p. A7-30). */
bool ThumbDecoder::DecodeBranchExchange(DecodedInsn* insn, uint16_t op) {
    const bool link = ((op >> 7) & 0x1u) != 0u;
    insn->rm = (((op >> 6) & 0x1u) << 3) | ((op >> 3) & 0x7u);
    if (link && insn->rm == 15u) return false;
    insn->kind = link ? ArmInsnKind::kBlxReg : ArmInsnKind::kBx;
    return true;
}

/* ARM DDI 0100I A7.1.39 LSL (2) (p. A7-66), A7.1.41 LSR (2) (p. A7-70),
   A7.1.12 ASR (2) (p. A7-17), A7.1.54 ROR (p. A7-92). */
bool ThumbDecoder::DecodeShiftByRegister(DecodedInsn* insn, uint16_t op,
                                         uint32_t type) {
    insn->op1      = 13u;
    insn->s        = 1u;
    insn->s_outside_it = 1u;
    insn->n        = type;
    insn->rd       =  op       & 0x7u;
    insn->rm       =  op       & 0x7u;
    insn->rs       = (op >> 3) & 0x7u;
    insn->kind     = ArmInsnKind::kDataProcessingShiftedReg;
    return true;
}

bool ThumbDecoder::DecodeAluOperations(DecodedInsn* insn, uint16_t op) {
    const uint32_t opcode = (op >> 6) & 0xFu;
    const uint32_t reg    =  op       & 0x7u;
    switch (opcode) {
    case 0x0u:
    case 0x1u:
    case 0x5u:
    case 0x6u:
    case 0xCu:
    case 0xEu:
        insn->rn = reg;
        insn->rd = reg;
        insn->s_outside_it = 1u;
        break;
    case 0x8u:
    case 0xAu:
    case 0xBu:
        insn->rn = reg;
        break;
    case 0xFu:
        insn->rd = reg;
        insn->s_outside_it = 1u;
        break;
    case 0x9u:
        /* ARM DDI 0100I A7.1.47 NEG, p. A7-80. */
        insn->op1       = 3u;
        insn->s         = 1u;
        insn->s_outside_it = 1u;
        insn->rn        = (op >> 3) & 0x7u;
        insn->rd        = reg;
        insn->immediate = 0u;
        insn->rs        = 0u;
        insn->kind      = ArmInsnKind::kDataProcessing;
        return true;
    case 0xDu: {
        /* ARM DDI 0100I A7.1.45 MUL, p. A7-77. */
        const uint32_t rm = (op >> 3) & 0x7u;
        if (rm == reg && !processor_config_->arch_v6) return false;
        insn->op1      = 0u;
        insn->s        = 1u;
        insn->s_outside_it = 1u;
        insn->rd       = reg;
        insn->rm       = reg;
        insn->rn       = rm;
        insn->kind     = ArmInsnKind::kMultiply;
        return true;
    }
    case 0x2u:
        return DecodeShiftByRegister(insn, op, kSrLsl);
    case 0x3u:
        return DecodeShiftByRegister(insn, op, kSrLsr);
    case 0x4u:
        return DecodeShiftByRegister(insn, op, kSrAsr);
    case 0x7u:
        return DecodeShiftByRegister(insn, op, kSrRor);
    default:
        return MarkArmUnimplemented(insn, op);
    }
    insn->op1      = opcode;
    insn->s        = 1u;
    insn->rm       = (op >> 3) & 0x7u;
    insn->n        = kSrLsl;
    insn->rs       = 0u;
    insn->kind     = ArmInsnKind::kDataProcessingReg;
    return true;
}

/* ARM DDI 0100I A7.1.7 ADD (5) (p. A7-10), A7.1.8 ADD (6) (p. A7-11). */
bool ThumbDecoder::DecodeAddToPcOrSp(DecodedInsn* insn, uint16_t op) {
    const uint32_t imm = (op & 0xFFu) * 4u;
    const bool     sp  = ((op >> 11) & 0x1u) != 0u;
    insn->op1       = 4u;
    insn->s         = 0u;
    insn->rs        = 0u;
    insn->rd        = (op >> 8) & 0x7u;
    insn->rn        = sp ? 13u : 15u;
    insn->immediate = sp ? imm : imm - ((insn->guest_address + 4u) & 3u);
    insn->kind      = ArmInsnKind::kDataProcessing;
    return true;
}

/* ARM DDI 0100I A7.1.13 B (1), p. A7-19. */
bool ThumbDecoder::DecodeConditionalBranch(DecodedInsn* insn, uint16_t op) {
    const uint32_t imm8 = op & 0xFFu;
    insn->l        = 0u;
    insn->offset   = static_cast<int32_t>(((imm8 ^ 0x80u) - 0x80u) << 1);
    insn->kind     = ArmInsnKind::kBranch;
    return true;
}

/* ARM DDI 0100I A7.1.14 B (2), p. A7-21. */
bool ThumbDecoder::DecodeUnconditionalBranch(DecodedInsn* insn, uint16_t op) {
    const uint32_t off11 = op & 0x7FFu;
    insn->l        = 0u;
    insn->offset   = static_cast<int32_t>(((off11 ^ 0x400u) - 0x400u) << 1);
    insn->kind     = ArmInsnKind::kBranch;
    return true;
}

/* ARM DDI 0100I A7.1.17 BL, BLX (1), H == 10 (p. A7-27). */
bool ThumbDecoder::DecodeBranchLinkPrefix(DecodedInsn* insn, uint16_t op) {
    const uint32_t off11 = op & 0x7FFu;
    insn->offset   = static_cast<int32_t>(((off11 ^ 0x400u) - 0x400u) << 12);
    insn->kind     = ArmInsnKind::kThumbBlPrefix;
    return true;
}

/* ARM DDI 0100I A7.1.17 BL, BLX (1), H == 11 (p. A7-27). */
bool ThumbDecoder::DecodeBranchLinkSuffix(DecodedInsn* insn, uint16_t op) {
    insn->offset   = static_cast<int32_t>((op & 0x7FFu) << 1);
    insn->kind     = ArmInsnKind::kThumbBlSuffix;
    return true;
}

/* ARM DDI 0100I Figure A6-1 (A6.2, p. A6-4): bits[15:11] select the Thumb
   instruction class. */
bool ThumbDecoder::DecodeThumb(DecodedInsn* insn, uint16_t op) {
    const uint32_t row = (op >> 11) & 0x1Fu;
    /* Figure A6-1, p. A6-4: the conditional branch row is the only encoding
       carrying a cond field. Note 2, p. A6-5: "The cond field is not allowed
       to be 1110 or 1111 in this line." */
    insn->cond = 0xEu;
    if (row == 0x1Au || row == 0x1Bu) {
        const uint32_t cond = (op >> 8) & 0xFu;
        if (cond < 0xEu) insn->cond = cond;
    }
    /* Figure A6-1, p. A6-4: branch/exchange, conditional branch, software
       interrupt, unconditional branch, BLX suffix and BL suffix write the PC. */
    const bool     high_line = row == 0x08u && ((op >> 10) & 0x1u) != 0u;
    const uint32_t opcode98  = (op >> 8) & 0x3u;
    const uint32_t high_rd   = (((op >> 7) & 0x1u) << 3) | (op & 0x7u);
    insn->r15_modified =
        (high_line && opcode98 == 0x3u) ||
        (high_line && (opcode98 == 0x0u || opcode98 == 0x2u) &&
         high_rd == 15u) ||
        row == 0x1Au || row == 0x1Bu || row == 0x1Cu || row == 0x1Du ||
        row == 0x1Fu;

    switch (row) {
    case 0x00u:
    case 0x01u:
    case 0x02u:
        return DecodeShiftByImmediate(insn, op);
    case 0x03u:
        return DecodeAddSubtract(insn, op);
    case 0x04u:
    case 0x05u:
    case 0x06u:
    case 0x07u:
        return DecodeImmediateOperations(insn, op);
    case 0x08u:
        /* Figure A6-1 note 3, p. A6-5. */
        if (((op >> 10) & 0x1u) != 0u && ((op >> 8) & 0x3u) == 0x3u &&
            ((op >> 7) & 0x1u) != 0u && !processor_config_->blx_reg) {
            return false;
        }
        if (((op >> 10) & 0x1u) != 0u && ((op >> 8) & 0x3u) != 0x3u) {
            return DecodeSpecialDataProcessing(insn, op);
        }
        if (((op >> 10) & 0x1u) != 0u && ((op >> 8) & 0x3u) == 0x3u) {
            return DecodeBranchExchange(insn, op);
        }
        return DecodeAluOperations(insn, op);
    case 0x09u:
        return transfer_decoder_->DecodeLoadLiteral(insn, op);
    case 0x0Au:
    case 0x0Bu:
        return transfer_decoder_->DecodeRegisterOffsetTransfer(insn, op);
    case 0x0Cu:
    case 0x0Du:
    case 0x0Eu:
    case 0x0Fu:
        return transfer_decoder_->DecodeImmediateOffsetTransfer(insn, op);
    case 0x10u:
    case 0x11u:
        return transfer_decoder_->DecodeHalfwordOffsetTransfer(insn, op);
    case 0x12u:
    case 0x13u:
        return transfer_decoder_->DecodeStackRelativeTransfer(insn, op);
    case 0x14u:
    case 0x15u:
        return DecodeAddToPcOrSp(insn, op);
    case 0x16u:
    case 0x17u:
        return stack_control_->DecodeStackControlGroup(insn, op);
    case 0x18u:
    case 0x19u:
        return transfer_decoder_->DecodeLoadStoreMultiple(insn, op);
    case 0x1Au:
        return DecodeConditionalBranch(insn, op);
    case 0x1Bu:
        switch ((op >> 8) & 0xFu) {
        case 0xEu:
            return false;
        case 0xFu:
            return MarkArmUnimplemented(insn, op);
        default:
            return DecodeConditionalBranch(insn, op);
        }
    case 0x1Cu:
        return DecodeUnconditionalBranch(insn, op);
    case 0x1Du:
        /* Figure A6-1 note 4, p. A6-5. */
        if ((op & 0x1u) != 0u || !processor_config_->blx_reg) return false;
        return MarkArmUnimplemented(insn, op);
    case 0x1Eu:
        return DecodeBranchLinkPrefix(insn, op);
    case 0x1Fu:
        return DecodeBranchLinkSuffix(insn, op);
    default:
        return MarkArmUnimplemented(insn, op);
    }
}

}
