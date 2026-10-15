#include "thumb32_data_proc_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "thumb32_fatal.h"

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32DataProcDecoder);

bool Thumb32DataProcDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void Thumb32DataProcDecoder::OnReady() {
    fatal_ = &engine_.Get<Thumb32Fatal>();
}

/* DDI 0406C.c Table A6-23 (A6.3.11) p. A6-244: A8.8.103 MOV (register, Thumb)
   p. A8-486, A8.8.94 LSL p. A8-468, A8.8.96 LSR p. A8-472, A8.8.16 ASR
   p. A8-330, A8.8.151 RRX p. A8-572, A8.8.149 ROR p. A8-568. */
bool Thumb32DataProcDecoder::DecodeMoveRegisterImmediateShifts(
    DecodedInsn* insn, uint32_t op) {
    const uint32_t s    = (op >> 20) & 0x1u;
    const uint32_t imm3 = (op >> 12) & 0x7u;
    const uint32_t rd   = (op >>  8) & 0xFu;
    const uint32_t imm2 = (op >>  6) & 0x3u;
    const uint32_t type = (op >>  4) & 0x3u;
    const uint32_t rm   =  op        & 0xFu;
    const uint32_t imm5 = (imm3 << 2) | imm2;

    if (type == kSrLsl && imm5 == 0u && s == 0u) {
        if (rd == 0xFu || rm == 0xFu || (rd == 13u && rm == 13u)) {
            return false;
        }
    } else if (rd == 13u || rd == 0xFu || rm == 13u || rm == 0xFu) {
        return false;
    }

    uint32_t shift_t = 0u;
    uint32_t shift_n = 0u;
    DecodeImmShift(type, imm5, &shift_t, &shift_n);

    insn->op1      = 13u;
    insn->s        = s;
    insn->rn       = 0xFu;
    insn->rd       = rd;
    insn->rm       = rm;
    insn->n        = shift_t;
    insn->rs       = shift_n;
    insn->place_fn = &PlaceDataProcessingReg;
    return true;
}

bool Thumb32DataProcDecoder::DecodeDataProcessingShiftedRegister(
    DecodedInsn* insn, uint32_t op) {
    const uint32_t o    = (op >> 21) & 0xFu;
    const uint32_t s    = (op >> 20) & 0x1u;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t imm3 = (op >> 12) & 0x7u;
    const uint32_t rd   = (op >>  8) & 0xFu;
    const uint32_t imm2 = (op >>  6) & 0x3u;
    const uint32_t type = (op >>  4) & 0x3u;
    const uint32_t rm   =  op        & 0xFu;

    /* A6.1.1 p. A6-220: a non-zero (0) bit is UNPREDICTABLE; hw2[15] is (0)
       on the shared T2 diagram A8.8.14 p. A8-326, and Table A6-9 p. A6-230
       leaves it unconsumed on the op1 = 01 path. */
    if (((op >> 15) & 0x1u) != 0u) {
        return false;
    }

    /* A8.8.125 PKH (p. A8-522), encoding T1: "if S == '1' || T == '1' then
       UNDEFINED", S at bit[4] of the first halfword and T at bit[4] of the
       second. */
    if (o == 0x6u) {
        if (s != 0u || ((op >> 4) & 0x1u) != 0u) {
            return false;
        }
        return MarkArmUnimplemented(insn, op);
    }
    if (o == 0x2u && rn == 0xFu) {
        return DecodeMoveRegisterImmediateShifts(insn, op);
    }

    uint32_t opcode = 0u;
    bool     test   = false;
    if (!MapDataProcessingOpcode(o, rn, rd, s, &opcode, &test)) {
        return false;
    }

    if (rm == 13u || rm == 0xFu) {
        return false;
    }
    bool sp_form = false;
    if (!DataProcRegistersValid(o, rn, rd, test, &sp_form)) {
        return false;
    }

    uint32_t shift_t = 0u;
    uint32_t shift_n = 0u;
    DecodeImmShift(type, (imm3 << 2) | imm2, &shift_t, &shift_n);
    /* A8.8.10 ADD (SP plus register, Thumb) (p. A8-318) and A8.8.226 SUB (SP
       minus register) (p. A8-718), encodings T3 and T1: "if d == 13 &&
       (shift_t != SRType_LSL || shift_n > 3) then UNPREDICTABLE". */
    if (sp_form && rd == 13u && (shift_t != kSrLsl || shift_n > 3u)) {
        return false;
    }

    insn->op1      = opcode;
    insn->s        = s;
    insn->rn       = rn;
    insn->rd       = rd;
    insn->rm       = rm;
    insn->n        = shift_t;
    insn->rs       = shift_n;
    insn->place_fn = &PlaceDataProcessingReg;
    return true;
}

/* ARM DDI 0406C.c Table A6-11 (A6.3.2, p. A6-232): the key i:imm3:a selects
   the placement of abcdefgh. Keys 00xxx replicate the byte into one, two or
   four positions; every other key is the rotation applied to 1bcdefgh. */
uint32_t Thumb32DataProcDecoder::ThumbExpandImm(uint32_t key,
                                                uint32_t imm8) const {
    if ((key >> 3) == 0u) {
        switch ((key >> 1) & 0x3u) {
        case 0u:  return imm8;
        case 1u:  return (imm8 << 16) | imm8;
        case 2u:  return (imm8 << 24) | (imm8 << 8);
        default:  return (imm8 << 24) | (imm8 << 16) | (imm8 << 8) | imm8;
        }
    }
    const uint32_t value = 0x80u | (imm8 & 0x7Fu);
    return (value >> key) | (value << (32u - key));
}

/* DDI 0406C.c Table A6-10 (A6.3.1) p. A6-231 and Table A6-22 (A6.3.11)
   p. A6-243; opcode values are the ARM numbering of Table A5-5 p. A5-199. */
bool Thumb32DataProcDecoder::MapDataProcessingOpcode(uint32_t o, uint32_t rn,
                                                     uint32_t rd, uint32_t s,
                                                     uint32_t* opcode,
                                                     bool* test) const {
    const bool wide_rd = rd == 0xFu && s != 0u;
    *test = false;
    switch (o) {
    case 0x0u: *test = wide_rd; *opcode = *test ? 8u : 0u; return true;
    case 0x1u: *opcode = 14u; return true;
    case 0x2u: *opcode = rn == 0xFu ? 13u : 12u; return true;
    case 0x3u:
        *opcode = rn == 0xFu ? 15u : static_cast<uint32_t>(kDpOrn);
        return true;
    case 0x4u: *test = wide_rd; *opcode = *test ? 9u : 1u; return true;
    case 0x8u: *test = wide_rd; *opcode = *test ? 11u : 4u; return true;
    case 0xAu: *opcode = 5u; return true;
    case 0xBu: *opcode = 6u; return true;
    case 0xDu: *test = wide_rd; *opcode = *test ? 10u : 2u; return true;
    case 0xEu: *opcode = 3u; return true;
    default:   return false;
    }
}

bool Thumb32DataProcDecoder::DataProcRegistersValid(uint32_t o, uint32_t rn,
                                                    uint32_t rd, bool test,
                                                    bool* sp_form) const {
    *sp_form = (o == 0x8u || o == 0xDu) && rn == 13u;
    if (!test && (rd == 0xFu || (rd == 13u && !*sp_form))) {
        return false;
    }
    if (*sp_form) {
        return true;
    }
    if ((o == 0x2u || o == 0x3u) && rn == 0xFu) {
        return true;
    }
    return rn != 0xFu && rn != 13u;
}

bool Thumb32DataProcDecoder::DecodeDataProcessingModifiedImmediate(
    DecodedInsn* insn, uint32_t op) {
    const uint32_t o    = (op >> 21) & 0xFu;
    const uint32_t s    = (op >> 20) & 0x1u;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t rd   = (op >>  8) & 0xFu;
    const uint32_t imm8 =  op        & 0xFFu;
    const uint32_t key  = (((op >> 26) & 0x1u) << 4) |
                          (((op >> 12) & 0x7u) << 1) | ((imm8 >> 7) & 0x1u);
    uint32_t opcode = 0u;
    bool     test   = false;
    if (!MapDataProcessingOpcode(o, rn, rd, s, &opcode, &test)) {
        return false;
    }
    bool sp_form = false;
    if (!DataProcRegistersValid(o, rn, rd, test, &sp_form)) {
        return false;
    }

    insn->op1       = opcode;
    insn->s         = s;
    insn->rn        = rn;
    insn->rd        = rd;
    insn->immediate = ThumbExpandImm(key, imm8);
    insn->rs        = key >= 8u ? key : 0u;
    insn->place_fn  = &PlaceDataProcessing;
    return true;
}

/* DDI 0406C.c A6.3.15 and Table A6-27 p. A6-248: op1 = bits[21:20],
   op2 = bits[5:4], Rn = bits[19:16], Rd = bits[11:8], Rm = bits[3:0];
   other encodings in this space are UNDEFINED. */
bool Thumb32DataProcDecoder::DecodeMiscellaneous(DecodedInsn* insn,
                                                 uint32_t op) {
    const uint32_t op1 = (op >> 20) & 0x3u;
    const uint32_t op2 = (op >>  4) & 0x3u;
    const uint32_t rn  = (op >> 16) & 0xFu;
    const uint32_t rd  = (op >>  8) & 0xFu;
    const uint32_t rm  =  op        & 0xFu;

    if (op1 == 0x0u) {
        /* A8.8.134 QADD p. A8-540, A8.8.138 QDADD p. A8-548, A8.8.141 QSUB
           p. A8-554, A8.8.139 QDSUB p. A8-550, encoding T1: "if d IN {13,15}
           || n IN {13,15} || m IN {13,15} then UNPREDICTABLE". */
        if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
            rm == 13u || rm == 0xFu) {
            return false;
        }
        /* Table A6-27 p. A6-248 orders op2 QADD / QDADD / QSUB / QDSUB;
           Table A5-8 p. A5-202 orders insn[22:21] QADD / QSUB / QDADD /
           QDSUB. */
        insn->op1      = ((op2 & 0x1u) << 1) | (op2 >> 1);
        insn->rn       = rn;
        insn->rd       = rd;
        insn->rm       = rm;
        insn->place_fn = &PlaceSaturatingArith;
        return true;
    }

    if (op1 == 0x2u) {
        if (op2 != 0x0u) {
            return false;
        }
        fatal_->Unimplemented("select bytes (A6-248)", insn, op);
    }
    if (op1 == 0x3u && op2 != 0x0u) {
        return false;
    }

    if (rn != rm || rd == 13u || rd == 0xFu || rm == 13u || rm == 0xFu) {
        return false;
    }
    insn->rd       = rd;
    insn->rm       = rm;
    insn->place_fn = op1 == 0x3u ? &PlaceClz
                   : op2 == 0x0u ? &PlaceRev
                   : op2 == 0x1u ? &PlaceRev16
                   : op2 == 0x2u ? &PlaceRbit
                                 : &PlaceRevsh;
    return true;
}

bool Thumb32DataProcDecoder::DecodeDataProcessingRegister(DecodedInsn* insn,
                                                          uint32_t op) {
    const uint32_t op1 = (op >> 20) & 0xFu;
    const uint32_t rn  = (op >> 16) & 0xFu;
    const uint32_t rd  = (op >>  8) & 0xFu;
    const uint32_t op2 = (op >>  4) & 0xFu;
    const uint32_t rm  =  op        & 0xFu;

    if (((op >> 12) & 0xFu) != 0xFu) {
        return false;
    }

    if ((op1 & 0x8u) == 0u && op2 == 0x0u) {
        if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
            rm == 13u || rm == 0xFu) {
            return false;
        }
        insn->op1      = 13u;
        insn->s        = op1 & 0x1u;
        insn->rd       = rd;
        insn->rm       = rn;
        insn->rs       = rm;
        insn->n        = (op1 >> 1) & 0x3u;
        insn->place_fn = &PlaceDataProcessingShiftedReg;
        return true;
    }

    /* A8.8.235 SXTH p. A8-734, A8.8.276 UXTH p. A8-816, A8.8.233 SXTB
       p. A8-730, A8.8.274 UXTB p. A8-812; rotate at bits[5:4], bit[6] is (0). */
    if (op1 <= 0x5u && (op2 & 0x8u) != 0u) {
        if ((op2 & 0x4u) != 0u) {
            return false;
        }
        /* Table A6-24 p. A6-245 rows 0010/0011: Rn == 1111 is A8.8.234
           SXTB16 p. A8-732 / A8.8.275 UXTB16 p. A8-814; Rn != 1111 is
           A8.8.231 SXTAB16 p. A8-726 / A8.8.272 UXTAB16 p. A8-808. */
        if (op1 == 0x2u || op1 == 0x3u) {
            fatal_->Unimplemented(rn == 0xFu
                                      ? "signed / unsigned extend byte 16 "
                                        "(A6-245)"
                                      : "signed / unsigned extend and add "
                                        "byte 16 (A6-245)",
                                  insn, op);
        }
        if (rd == 13u || rd == 0xFu || rm == 13u || rm == 0xFu || rn == 13u) {
            return false;
        }
        insn->rd  = rd;
        insn->rm  = rm;
        insn->op1 = (op >> 4) & 0x3u;
        if (rn != 0xFu) {
            insn->rn = rn;
            insn->n  = 1u;
        }
        switch (op1) {
        case 0x0u: insn->place_fn = &PlaceSxth; break;
        case 0x1u: insn->place_fn = &PlaceUxth; break;
        case 0x4u: insn->place_fn = &PlaceSxtb; break;
        default:   insn->place_fn = &PlaceUxtb; break;
        }
        return true;
    }
    if ((op1 & 0x8u) != 0u && (op2 & 0xCu) == 0x0u) {
        fatal_->Unimplemented(
            "parallel addition and subtraction, signed (A6-246)", insn, op);
    }
    if ((op1 & 0x8u) != 0u && (op2 & 0xCu) == 0x4u) {
        fatal_->Unimplemented(
            "parallel addition and subtraction, unsigned (A6-247)", insn, op);
    }
    if ((op1 & 0xCu) == 0x8u && (op2 & 0xCu) == 0x8u) {
        return DecodeMiscellaneous(insn, op);
    }
    return false;
}

}
