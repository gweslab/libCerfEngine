#pragma once

#include <cstdint>

#include <cerfengine/jit_block.h>

namespace cerfengine {

struct MipsDecodedInsn;
struct MipsBlockContext;

using MipsPlaceFn = uint8_t* (*)(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);

struct MipsDecodedInsn {
    MipsPlaceFn place_fn;
    uint32_t    guest_address;
    uint32_t    raw;

    /* R/I/J fields. Bit positions per QEMU translate.c decode_opc:
       op=word>>26, rs=word>>21, rt=word>>16, rd=word>>11, sa=word>>6 (&0x1f),
       funct=word&0x3f, imm=word&0xffff, target=word&0x3ffffff. */
    uint32_t op     : 6;
    uint32_t rs     : 5;
    uint32_t rt     : 5;
    uint32_t rd     : 5;
    uint32_t sa     : 5;
    uint32_t funct  : 6;
    uint32_t imm;
    uint32_t target;

    uint32_t length;

    uint32_t is_branch : 1;
    uint32_t is_likely : 1;
    uint32_t ends_block : 1;

    JitBlock* entry_point;
    uint8_t*  jmp_fixup_location;
};

}
