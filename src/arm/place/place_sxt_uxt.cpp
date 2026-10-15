#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

/* DDI 0406C A8.8.233/.235/.274/.276 A1: rotation = UInt(rotate:'000'),
   a ROR by 0/8/16/24 before extraction. */
inline void EmitLoadAndRotate(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rm));
    const uint32_t rotate_bits = d->op1 * 8u;
    if (rotate_bits != 0u) {
        EmitRorReg32Imm(cursor, kEax, static_cast<uint8_t>(rotate_bits));
    }
}

/* DDI 0406C.c A8.8.230 SXTAB (p. A8-725), A8.8.232 SXTAH (p. A8-729),
   A8.8.271 UXTAB (p. A8-807), A8.8.273 UXTAH (p. A8-811) Operation:
   "R[d] = R[n] + SignExtend(rotated<7:0>, 32)" and its three siblings. */
inline void EmitAddRn(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    if (d->n != 0u) {
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rn));
    }
}

inline void EmitStoreRd(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
}

}

uint8_t* PlaceSxtb(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;
    EmitLoadAndRotate (cursor, d);
    EmitMovsxReg32Reg8(cursor, kEax, kAl);
    EmitAddRn         (cursor, d);
    EmitStoreRd       (cursor, d);
    return cursor;
}

uint8_t* PlaceUxtb(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;
    EmitLoadAndRotate (cursor, d);
    EmitMovzxReg32Reg8(cursor, kEax, kAl);
    EmitAddRn         (cursor, d);
    EmitStoreRd       (cursor, d);
    return cursor;
}

uint8_t* PlaceSxth(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;
    EmitLoadAndRotate  (cursor, d);
    EmitMovsxReg32Reg16(cursor, kEax, kEax);
    EmitAddRn          (cursor, d);
    EmitStoreRd        (cursor, d);
    return cursor;
}

uint8_t* PlaceUxth(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;
    EmitLoadAndRotate  (cursor, d);
    EmitMovzxReg32Reg16(cursor, kEax, kEax);
    EmitAddRn          (cursor, d);
    EmitStoreRd        (cursor, d);
    return cursor;
}

}
