#include "thumb32_branch_system_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "thumb32_fatal.h"

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32BranchSystemDecoder);

bool Thumb32BranchSystemDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void Thumb32BranchSystemDecoder::OnReady() {
    fatal_ = &engine_.Get<Thumb32Fatal>();
}

/* DDI 0406C.c A8.8.18 B encoding T4 (p. A8-334) and A8.8.25 BL encoding T1
   (p. A8-348): I1 = NOT(J1 EOR S), I2 = NOT(J2 EOR S), imm32 =
   SignExtend(S:I1:I2:imm10:imm11:'0', 32). */
int32_t Thumb32BranchSystemDecoder::BranchOffset25(uint32_t op) const {
    const uint32_t s     = (op >> 26) & 0x1u;
    const uint32_t i1    = (((op >> 13) & 0x1u) ^ s) ^ 0x1u;
    const uint32_t i2    = (((op >> 11) & 0x1u) ^ s) ^ 0x1u;
    const uint32_t imm10 = (op >> 16) & 0x3FFu;
    const uint32_t imm11 =  op        & 0x7FFu;
    const uint32_t value = (s << 24) | (i1 << 23) | (i2 << 22) |
                           (imm10 << 12) | (imm11 << 1);
    return static_cast<int32_t>(value << 7) >> 7;
}

/* DDI 0406C.c A8.8.18 B encoding T3 (p. A8-334): cond = bits[25:22], imm32 =
   SignExtend(S:J2:J1:imm6:imm11:'0', 32), and "if InITBlock() then
   UNPREDICTABLE". */
bool Thumb32BranchSystemDecoder::DecodeConditionalBranch(DecodedInsn* insn,
                                                         uint32_t op) {
    const uint32_t s     = (op >> 26) & 0x1u;
    const uint32_t j1    = (op >> 13) & 0x1u;
    const uint32_t j2    = (op >> 11) & 0x1u;
    const uint32_t imm6  = (op >> 16) & 0x3Fu;
    const uint32_t imm11 =  op        & 0x7FFu;
    const uint32_t value = (s << 20) | (j2 << 19) | (j1 << 18) |
                           (imm6 << 12) | (imm11 << 1);

    insn->cond         = (op >> 22) & 0xFu;
    insn->l            = 0u;
    insn->offset       = static_cast<int32_t>(value << 11) >> 11;
    insn->r15_modified = true;
    insn->place_fn     = &PlaceBranch;
    return true;
}

bool Thumb32BranchSystemDecoder::DecodeBranch(DecodedInsn* insn, uint32_t op) {
    insn->l            = 0u;
    insn->offset       = BranchOffset25(op);
    insn->r15_modified = true;
    insn->place_fn     = &PlaceBranch;
    return true;
}

bool Thumb32BranchSystemDecoder::DecodeBranchLink(DecodedInsn* insn,
                                                  uint32_t op, bool exchange) {
    if (exchange) {
        if ((op & 0x1u) != 0u) {
            return false;
        }
        const uint32_t s      = (op >> 26) & 0x1u;
        const uint32_t i1     = (((op >> 13) & 0x1u) ^ s) ^ 0x1u;
        const uint32_t i2     = (((op >> 11) & 0x1u) ^ s) ^ 0x1u;
        const uint32_t imm10h = (op >> 16) & 0x3FFu;
        const uint32_t imm10l = (op >>  1) & 0x3FFu;
        const uint32_t value  = (s << 24) | (i1 << 23) | (i2 << 22) |
                                (imm10h << 12) | (imm10l << 2);
        insn->offset   = static_cast<int32_t>(value << 7) >> 7;
        insn->place_fn = &PlaceThumbBlxImm;
    } else {
        insn->l        = 1u;
        insn->offset   = BranchOffset25(op);
        insn->place_fn = &PlaceBranch;
    }
    insn->r15_modified = true;
    return true;
}

/* DDI 0406C.c B9.3.12 MSR (register) encoding T1 (p. B9-1998), whose
   mask<1:0> == '00' subset is A8.8.112 (p. A8-500): R = bit[20], Rn =
   bits[19:16], mask = bits[11:8]; "if n IN {13,15} then UNPREDICTABLE". */
bool Thumb32BranchSystemDecoder::DecodeMoveToSpecial(DecodedInsn* insn,
                                                     uint32_t op) {
    /* Table A6-13 p. A6-235: imm8 = xx1xxxxx is B9.3.10 MSR (Banked
       register) p. B9-1994, a Virtualization Extensions encoding. */
    if (((op >> 5) & 0x1u) != 0u) {
        return false;
    }
    /* B9.3.12 T1 (p. B9-1998) marks hw2[13], hw2[7:6] and hw2[4:0] (0);
       A6.1.1 (p. A6-220) makes a marked (0) that is not 0 UNPREDICTABLE. */
    if (((op >> 13) & 0x1u) != 0u || ((op >> 6) & 0x3u) != 0u ||
        (op & 0x1Fu) != 0u) {
        return false;
    }
    const uint32_t rn = (op >> 16) & 0xFu;
    if (rn == 13u || rn == 0xFu) {
        return false;
    }
    insn->s        = 1u;
    insn->n        = (op >> 20) & 0x1u;
    insn->rm       = rn;
    insn->crn      = (op >>  8) & 0xFu;
    /* QEMU ARM translate.c trans_MSR_reg -> gen_set_psr ->
       gen_lookup_tb (DISAS_EXIT). */
    insn->context_sync = true;
    insn->place_fn = &PlaceMRSorMSR;
    return true;
}

/* DDI 0406C.c B9.3.8 MRS encoding T1 (p. B9-1990), whose R == '0' subset is
   A8.8.109 (p. A8-496): R = bit[20], Rd = bits[11:8]; "if d IN {13,15} then
   UNPREDICTABLE". */
bool Thumb32BranchSystemDecoder::DecodeMoveFromSpecial(DecodedInsn* insn,
                                                        uint32_t op) {
    /* Table A6-13 p. A6-235: imm8 = xx1xxxxx is B9.3.9 MRS (Banked
       register) p. B9-1992, a Virtualization Extensions encoding. */
    if (((op >> 5) & 0x1u) != 0u) {
        return false;
    }
    /* B9.3.8 T1 (p. B9-1990) marks hw1[3:0] (1) and hw2[13], hw2[7:6],
       hw2[4:0] (0); A6.1.1 (p. A6-220) makes a violated marked bit
       UNPREDICTABLE. */
    if (((op >> 16) & 0xFu) != 0xFu || ((op >> 13) & 0x1u) != 0u ||
        ((op >> 6) & 0x3u) != 0u || (op & 0x1Fu) != 0u) {
        return false;
    }
    const uint32_t rd = (op >> 8) & 0xFu;
    if (rd == 13u || rd == 0xFu) {
        return false;
    }
    insn->s        = 0u;
    insn->n        = (op >> 20) & 0x1u;
    insn->rd       = rd;
    insn->place_fn = &PlaceMRSorMSR;
    return true;
}

/* DDI 0406C.c B9.3.1 CPS (Thumb) encoding T2 (p. B9-1978): imod = hw2[10:9],
   M = hw2[8], A:I:F = hw2[7:5], mode = hw2[4:0]; A, I and F occupy CPSR bits
   8, 7 and 6 (CPSRWriteByInstr, p. B1-1153). */
bool Thumb32BranchSystemDecoder::DecodeChangeProcessorState(DecodedInsn* insn,
                                                            uint32_t op) {
    const uint32_t imod = (op >> 9) & 0x3u;
    const uint32_t m    = (op >> 8) & 0x1u;
    const uint32_t aif  = (op >> 5) & 0x7u;
    const uint32_t mode =  op       & 0x1Fu;

    if (mode != 0u && m == 0u) return false;
    if (((imod >> 1) != 0u) != (aif != 0u)) return false;
    if (imod == 0x1u) return false;

    const uint32_t aif_mask = aif << 6;
    insn->op1       = aif_mask | (m != 0u ? 0x1Fu : 0u);
    insn->immediate = (imod == 0x3u ? aif_mask : 0u) | (m != 0u ? mode : 0u);
    insn->und_in_it = 1u;
    /* QEMU ARM translate.c trans_CPS -> gen_set_psr_im ->
       gen_lookup_tb (DISAS_EXIT). */
    insn->context_sync = true;
    insn->place_fn  = &PlaceCps;
    return true;
}

bool Thumb32BranchSystemDecoder::DecodeCpsAndHints(DecodedInsn* insn,
                                                    uint32_t op) {
    /* B9.3.1 CPS T2 (p. B9-1978) and A8.8.119 NOP T2 (p. A8-510) both mark
       hw1[3:0] (1) and hw2[13], hw2[11] (0); A6.1.1 (p. A6-220) makes a
       violated marked bit UNPREDICTABLE. */
    if (((op >> 16) & 0xFu) != 0xFu || ((op >> 13) & 0x1u) != 0u ||
        ((op >> 11) & 0x1u) != 0u) {
        return false;
    }
    const uint32_t hint_op1 = (op >> 8) & 0x7u;
    const uint32_t hint_op2 =  op       & 0xFFu;
    if (hint_op1 != 0u) {
        return DecodeChangeProcessorState(insn, op);
    }
    if (hint_op2 == 0x03u) {
        /* A8.8.425 WFI p. A8-1106. */
        insn->place_fn = &PlaceWfi;
        return true;
    }
    insn->place_fn = &PlaceNop;
    return true;
}

bool Thumb32BranchSystemDecoder::MiscControlBitsValid(uint32_t op) const {
    return ((op >> 16) & 0xFu) == 0xFu &&
           ((op >> 13) & 0x1u) == 0u   &&
           ((op >>  8) & 0xFu) == 0xFu;
}

/* DDI 0406C.c Table A6-15 p. A6-237: op = bits[7:4]; row 0000 carries
   footnote a, "This instruction is a NOP in Thumb state"; other encodings in
   this space are UNDEFINED in ARMv7. */
bool Thumb32BranchSystemDecoder::DecodeControlInstructions(DecodedInsn* insn,
                                                           uint32_t op) {
    switch ((op >> 4) & 0xFu) {
    case 0x0u:
        if (!MiscControlBitsValid(op) || (op & 0xFu) != 0xFu) {
            return false;
        }
        insn->und_in_it = 1u;
        insn->place_fn  = &PlaceNop;
        return true;
    case 0x1u:
        fatal_->Unimplemented("enter ThumbEE state (A6-237)", insn, op);
    case 0x2u:
        /* A8.8.32 CLREX (p. A8-360) also marks hw2[3:0] (1); its Operation
           (p. A8-361) is "ClearExclusiveLocal(ProcessorID())". */
        if (!MiscControlBitsValid(op) || (op & 0xFu) != 0xFu) {
            return false;
        }
        insn->place_fn = &PlaceClrex;
        return true;
    case 0x4u:
    case 0x5u:
        /* A8.8.44 DSB p. A8-380, A8.8.43 DMB p. A8-378, both with option at
           hw2[3:0]. */
        if (!MiscControlBitsValid(op)) {
            return false;
        }
        insn->place_fn = &PlaceNop;
        return true;
    case 0x6u:
        /* A8.8.53 ISB p. A8-389; Glossary "Context synchronization
           operation" names an ISB operation and excludes DSB and DMB. */
        if (!MiscControlBitsValid(op)) {
            return false;
        }
        insn->context_sync = true;
        insn->place_fn     = &PlaceNop;
        return true;
    default:
        return false;
    }
}

bool Thumb32BranchSystemDecoder::DecodeExceptionReturn(DecodedInsn* insn,
                                                        uint32_t op) {
    /* B9.3.19 T1 (p. B9-2010) marks hw1[3:0] (1)(1)(1)(0) and hw2[11:8] (1),
       hw2[13] (0); A6.1.1 (p. A6-220) makes a violated marked bit
       UNPREDICTABLE. */
    if (((op >> 16) & 0xFu) != 0xEu || ((op >> 13) & 0x1u) != 0u ||
        ((op >> 8) & 0xFu) != 0xFu) {
        return false;
    }
    insn->op1                 = 2u;
    insn->s                   = 1u;
    insn->rn                  = 14u;
    insn->rd                  = 15u;
    insn->rs                  = 0u;
    insn->immediate           = op & 0xFFu;
    insn->r15_modified        = true;
    insn->is_exception_return = true;
    insn->place_fn            = &PlaceDataProcessing;
    return true;
}

/* DDI 0406C.c Table A6-13 p. A6-235, the op = 0111xxx rows keyed on
   bits[22:20]. */
bool Thumb32BranchSystemDecoder::DecodeSystemGroup(DecodedInsn* insn,
                                                    uint32_t op) {
    switch ((op >> 20) & 0x7u) {
    case 0x0u:
    case 0x1u:
        return DecodeMoveToSpecial(insn, op);
    case 0x2u:
        return DecodeCpsAndHints(insn, op);
    case 0x3u:
        return DecodeControlInstructions(insn, op);
    case 0x4u:
        /* A8.8.28 BXJ p. A8-354. */
        fatal_->Unimplemented("branch and exchange Jazelle (A6-235)", insn, op);
    case 0x5u:
        return DecodeExceptionReturn(insn, op);
    default:
        return DecodeMoveFromSpecial(insn, op);
    }
}

/* DDI 0406C.c A6.3.4 and Table A6-13 pp. A6-235/236: op = bits[26:20],
   op1 = bits[14:12]; other encodings in this space are UNDEFINED. */
bool Thumb32BranchSystemDecoder::Decode(DecodedInsn* insn, uint32_t op) {
    const uint32_t o   = (op >> 20) & 0x7Fu;
    const uint32_t op1 = (op >> 12) & 0x7u;

    switch (op1 & 0x5u) {
    case 0x5u: return DecodeBranchLink(insn, op, false);
    case 0x4u: return DecodeBranchLink(insn, op, true);
    case 0x1u: return DecodeBranch(insn, op);
    default:   break;
    }

    if (((o >> 3) & 0x7u) != 0x7u) {
        return DecodeConditionalBranch(insn, op);
    }
    if ((o & 0x40u) == 0u) {
        return DecodeSystemGroup(insn, op);
    }
    /* Table A6-13 p. A6-235: op1 = 000 with op = 1111111 is B9.3.14 SMC
       (p. B9-2002). Its op = 1111110 sibling is HVC and the op1 = 010 row is
       A8.8.247 UDF (p. A8-758), permanently UNDEFINED. */
    if (op1 == 0x0u && (o & 0x7u) == 0x7u) {
        fatal_->Unimplemented("secure monitor call (A6-235)", insn, op);
    }
    return false;
}

}
