#pragma once

#include <cstdint>

#include <cerfengine/jit_block.h>
#include <cerfengine/mips/mips_insn_kind.h>

namespace cerfengine {

struct MipsDecodedInsn {
    MipsInsnKind kind;

    /* R/I/J fields. Bit positions per QEMU translate.c decode_opc:
       op=word>>26, rs=word>>21, rt=word>>16, rd=word>>11, sa=word>>6 (&0x1f),
       funct=word&0x3f, imm=word&0xffff, target=word&0x3ffffff. */
    uint8_t op;
    uint8_t rs;
    uint8_t rt;
    uint8_t rd;
    uint8_t sa;
    uint8_t funct;

    uint8_t length;
    uint8_t is_branch;
    uint8_t is_likely;
    uint8_t ends_block;

    uint32_t guest_address;
    uint32_t raw;
    uint32_t imm;
    uint32_t target;
};

}
