#pragma once

#include <cstdint>

#include "mips_decoded_insn.h"

namespace cerfengine {

enum class Mips16DecodeKind {
    kSynth,
    kDirect,
    kReserved,
};

/* MIPS16 decode: VR4100 Series UM U15509EJ2V0UM ch.3 (formats 3.6 p59-63,
   opcode maps 3.7 p64-66, details 3.8.4 p71-83); QEMU
   mips16e_translate.c.inc decode_ase_mips16e / decode_extended_mips16_opc. */
class Mips16Decoder {
public:
    void Configure(bool has_64bit) { has_64bit_ = has_64bit; }

    /* EXTEND (op 11110) and JAL/JALX (op 00011) are 32 bits and may cross a
       word boundary (U15509EJ2V0UM 3.6 p59, Table 3-5 p64). */
    static bool Needs4Bytes(uint16_t hw0) {
        const uint32_t op = hw0 >> 11;
        return op == 0x1Eu || op == 0x03u;
    }

    Mips16DecodeKind Decode(uint16_t hw0, uint16_t hw1, uint32_t pc,
                            uint32_t base_pc, MipsDecodedInsn* d,
                            uint32_t* synth) const;

private:
    Mips16DecodeKind DecodeBase(uint16_t hw0, uint16_t hw1, uint32_t pc,
                                uint32_t base_pc, MipsDecodedInsn* d,
                                uint32_t* synth) const;
    Mips16DecodeKind DecodeExtended(uint16_t ext_hw, uint16_t insn_hw,
                                    uint32_t pc, uint32_t base_pc,
                                    MipsDecodedInsn* d, uint32_t* synth) const;
    Mips16DecodeKind DecodeRr(uint16_t hw0, MipsDecodedInsn* d,
                              uint32_t* synth) const;
    Mips16DecodeKind DecodeI64(uint32_t funct, uint32_t ry_field, uint16_t hw0,
                               bool extended, uint32_t ext_imm16,
                               uint32_t base_pc, MipsDecodedInsn* d,
                               uint32_t* synth) const;

    /* U15509EJ2V0UM Table 3-1 p55: MIPS16 register encodings 0-7 -> $16,$17,$2-$7. */
    static uint32_t Xlat(uint32_t r3) {
        constexpr uint32_t kMap[8] = {16, 17, 2, 3, 4, 5, 6, 7};
        return kMap[r3 & 7u];
    }

    static uint32_t R(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t sa,
                      uint32_t funct) {
        return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct;
    }

    static uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm16) {
        return (op << 26) | (rs << 21) | (rt << 16) | (imm16 & 0xFFFFu);
    }

    static uint32_t Sext(uint32_t v, uint32_t bits) {
        const uint32_t sh = 32u - bits;
        return static_cast<uint32_t>(static_cast<int32_t>(v << sh) >> sh);
    }

    bool has_64bit_ = false;
};

}
