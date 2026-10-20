#pragma once

#include <bit>
#include <cstdint>

namespace cerfengine {

/* ARM DDI 0406C.c A5.2.4 (p. A5-201) ARMExpandImm_C: ZeroExtend(imm12<7:0>)
   rotated right by 2*UInt(imm12<11:8>). */
constexpr uint32_t ArmExpandImm(uint32_t imm12) {
    return std::rotr(imm12 & 0xFFu, static_cast<int>(((imm12 >> 8) & 0xFu) * 2u));
}

}
