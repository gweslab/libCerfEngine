#include "mips16_decoder.h"

#include "mips_opcode.h"
#include "mips_place_fns.h"

namespace cerfengine {

Mips16DecodeKind Mips16Decoder::DecodeExtended(uint16_t ext_hw, uint16_t insn_hw,
                                               uint32_t pc, uint32_t base_pc,
                                               MipsDecodedInsn* d,
                                               uint32_t* synth) const {
    const uint32_t op    = insn_hw >> 11;
    const uint32_t rx    = Xlat(insn_hw >> 8);
    const uint32_t ry    = Xlat(insn_hw >> 5);
    const uint32_t imm16 = ((ext_hw & 0x1Fu) << 11) |
                           (((ext_hw >> 5) & 0x3Fu) << 5) | (insn_hw & 0x1Fu);

    switch (op) {
        case 0x00:
            *synth = I(MipsOp::kADDIU, 29, rx, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x01:
            d->rt       = rx;
            d->target   = (base_pc & ~3u) + Sext(imm16, 16);
            d->place_fn = &PlaceMips16Addiupc;
            return Mips16DecodeKind::kDirect;
        case 0x02:
            d->target     = pc + 4u + (Sext(imm16, 16) << 1);
            d->place_fn   = &PlaceMips16B;
            d->ends_block = 1;
            return Mips16DecodeKind::kDirect;
        case 0x04:
        case 0x05:
            d->rs         = rx;
            d->target     = pc + 4u + (Sext(imm16, 16) << 1);
            d->place_fn   = (op == 0x04u) ? &PlaceMips16Beqz : &PlaceMips16Bnez;
            d->ends_block = 1;
            return Mips16DecodeKind::kDirect;
        case 0x06: {
            const uint32_t sa5 = (ext_hw >> 6) & 0x1Fu;
            const uint32_t s5  = (ext_hw >> 5) & 1u;
            switch (insn_hw & 3u) {
                case 0:
                    if (s5) return Mips16DecodeKind::kReserved;
                    *synth = R(0, ry, rx, sa5, MipsSpecial::kSLL);
                    break;
                case 1: {
                    if (!has_64bit_) return Mips16DecodeKind::kReserved;
                    const uint32_t sh = (s5 << 5) | sa5;
                    *synth = (sh >= 32u)
                                 ? R(0, ry, rx, sh - 32u, MipsSpecial::kDSLL32)
                                 : R(0, ry, rx, sh, MipsSpecial::kDSLL);
                    break;
                }
                case 2:
                    if (s5) return Mips16DecodeKind::kReserved;
                    *synth = R(0, ry, rx, sa5, MipsSpecial::kSRL);
                    break;
                case 3:
                    if (s5) return Mips16DecodeKind::kReserved;
                    *synth = R(0, ry, rx, sa5, MipsSpecial::kSRA);
                    break;
            }
            return Mips16DecodeKind::kSynth;
        }
        case 0x07:
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = I(MipsOp::kLD, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x08: {
            uint32_t imm = (insn_hw & 0xFu) | (((ext_hw >> 4) & 0x7Fu) << 4) |
                           ((ext_hw & 0xFu) << 11);
            imm = Sext(imm, 15);
            if ((insn_hw >> 4) & 1u) {
                if (!has_64bit_) return Mips16DecodeKind::kReserved;
                *synth = I(MipsOp::kDADDIU, rx, ry, imm);
            } else {
                *synth = I(MipsOp::kADDIU, rx, ry, imm);
            }
            return Mips16DecodeKind::kSynth;
        }
        case 0x09:
            *synth = I(MipsOp::kADDIU, rx, rx, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x0A:
            *synth = I(MipsOp::kSLTI, rx, 24, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x0B:
            *synth = I(MipsOp::kSLTIU, rx, 24, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x0C:  /* i8: only bteqz/btnez/swrasp/adjsp extend (Table 3-13 p69) */
            switch ((insn_hw >> 8) & 7u) {
                case 0:
                case 1:
                    d->rs         = 24;
                    d->target     = pc + 4u + (Sext(imm16, 16) << 1);
                    d->place_fn   = (((insn_hw >> 8) & 7u) == 0u)
                                        ? &PlaceMips16Beqz
                                        : &PlaceMips16Bnez;
                    d->ends_block = 1;
                    return Mips16DecodeKind::kDirect;
                case 2:
                    *synth = I(MipsOp::kSW, 29, 31, imm16);
                    return Mips16DecodeKind::kSynth;
                case 3:
                    *synth = I(MipsOp::kADDIU, 29, 29, imm16);
                    return Mips16DecodeKind::kSynth;
                default:
                    return Mips16DecodeKind::kReserved;
            }
        case 0x0D:
            *synth = I(MipsOp::kORI, 0, rx, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x0E:
            *synth = I(MipsOp::kXORI, rx, 24, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x0F:
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = I(MipsOp::kSD, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x10:
            *synth = I(MipsOp::kLB, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x11:
            *synth = I(MipsOp::kLH, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x12:
            *synth = I(MipsOp::kLW, 29, rx, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x13:
            *synth = I(MipsOp::kLW, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x14:
            *synth = I(MipsOp::kLBU, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x15:
            *synth = I(MipsOp::kLHU, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x16:
            d->rt       = rx;
            d->target   = (base_pc & ~3u) + Sext(imm16, 16);
            d->place_fn = &PlaceMips16Lwpc;
            return Mips16DecodeKind::kDirect;
        case 0x17:
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = I(MipsOp::kLWU, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x18:
            *synth = I(MipsOp::kSB, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x19:
            *synth = I(MipsOp::kSH, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x1A:
            *synth = I(MipsOp::kSW, 29, rx, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x1B:
            *synth = I(MipsOp::kSW, rx, ry, imm16);
            return Mips16DecodeKind::kSynth;
        case 0x1D: {  /* ext-shift64: dsrl/dsra only, rx field 000, shamt =
                         ext[10:6] | S5<<5 (p63; Table 3-13 p69) */
            if (!has_64bit_ || ((insn_hw >> 8) & 7u) != 0u) {
                return Mips16DecodeKind::kReserved;
            }
            const uint32_t sh = (((ext_hw >> 5) & 1u) << 5) |
                                ((ext_hw >> 6) & 0x1Fu);
            switch (insn_hw & 0x1Fu) {
                case 0x08:  /* dsrl (Table 3-6 p64) */
                    *synth = (sh >= 32u)
                                 ? R(0, ry, ry, sh - 32u, MipsSpecial::kDSRL32)
                                 : R(0, ry, ry, sh, MipsSpecial::kDSRL);
                    return Mips16DecodeKind::kSynth;
                case 0x13:  /* dsra (Table 3-6 p64) */
                    *synth = (sh >= 32u)
                                 ? R(0, ry, ry, sh - 32u, MipsSpecial::kDSRA32)
                                 : R(0, ry, ry, sh, MipsSpecial::kDSRA);
                    return Mips16DecodeKind::kSynth;
                default:  /* "Only these two RR instructions can be extended"
                             (Table 3-6 Note 2 p64) */
                    return Mips16DecodeKind::kReserved;
            }
        }
        case 0x1F:  /* i64 (Table 3-11 p66) */
            return DecodeI64((insn_hw >> 8) & 7u, (insn_hw >> 5) & 7u, insn_hw,
                             true, imm16, base_pc, d, synth);
        default:  /* jal/rrr/extend are not extendable (Table 3-13 p69) */
            return Mips16DecodeKind::kReserved;
    }
}

/* I64 minors, Table 3-11 p66; short-form immediates per p72/73/75; extended
   forms take the raw imm16 (QEMU decode_i64_mips16 `extended ? offset :
   offset << 3`). */
Mips16DecodeKind Mips16Decoder::DecodeI64(uint32_t funct, uint32_t ry_field,
                                          uint16_t hw0, bool extended,
                                          uint32_t ext_imm16, uint32_t base_pc,
                                          MipsDecodedInsn* d,
                                          uint32_t* synth) const {
    if (!has_64bit_) return Mips16DecodeKind::kReserved;
    const uint32_t ry   = Xlat(ry_field);
    const uint32_t off5 = hw0 & 0x1Fu;

    switch (funct) {
        case 0:  /* ldsp: LD ry, off5<<3(sp) (p72); Load Doubleword is
                    extendable only as EXT-RRI (Table 3-13 p69) */
            if (extended) return Mips16DecodeKind::kReserved;
            *synth = I(MipsOp::kLD, 29, ry, off5 << 3);
            return Mips16DecodeKind::kSynth;
        case 1:
            *synth = I(MipsOp::kSD, 29, ry, extended ? ext_imm16 : off5 << 3);
            return Mips16DecodeKind::kSynth;
        case 2:
            *synth = I(MipsOp::kSD, 29, 31,
                       extended ? ext_imm16 : (hw0 & 0xFFu) << 3);
            return Mips16DecodeKind::kSynth;
        case 3:
            *synth = I(MipsOp::kDADDIU, 29, 29,
                       extended ? ext_imm16 : Sext(hw0 & 0xFFu, 8) << 3);
            return Mips16DecodeKind::kSynth;
        case 4:  /* ldpc: LD ry, off5<<3(pc), BasePC low 3 bits cleared (p72);
                    Load Doubleword is extendable only as EXT-RRI (Table 3-13
                    p69) */
            if (extended) return Mips16DecodeKind::kReserved;
            d->rt       = ry;
            d->target   = (base_pc & ~7u) + (off5 << 3);
            d->place_fn = &PlaceMips16Ldpc;
            return Mips16DecodeKind::kDirect;
        case 5:
            *synth = I(MipsOp::kDADDIU, ry, ry,
                       extended ? ext_imm16 : Sext(off5, 5));
            return Mips16DecodeKind::kSynth;
        case 6:
            d->rt     = ry;
            d->target = (base_pc & ~3u) +
                        (extended ? Sext(ext_imm16, 16) : off5 << 2);
            d->place_fn = &PlaceMips16Addiupc;
            return Mips16DecodeKind::kDirect;
        case 7:
            *synth = I(MipsOp::kDADDIU, 29, ry,
                       extended ? ext_imm16 : off5 << 2);
            return Mips16DecodeKind::kSynth;
        default:
            return Mips16DecodeKind::kReserved;
    }
}

}
