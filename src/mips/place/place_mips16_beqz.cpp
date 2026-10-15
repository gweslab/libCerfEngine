#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* MIPS16 BEQZ rx / BTEQZ (T=$24) (U15509EJ2V0UM Table 3-19 p83): taken iff
   the 64-bit gpr[rs] == 0; no delay slot (ibid. + 3.8.3 p70). */
uint8_t* PlaceMips16Beqz(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    constexpr int32_t kPcOff = static_cast<int32_t>(offsetof(MipsCpuState, pc));
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitOrRegBaseDisp32 (cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rs));
    uint8_t* j_nz = EmitJnzLabel(cursor);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, kPcOff, d->target);
    uint8_t* j_done = EmitJmpLabel(cursor);
    FixupLabel(j_nz, cursor);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, kPcOff,
                           d->guest_address + d->length);
    FixupLabel(j_done, cursor);
    return cursor;
}

}
