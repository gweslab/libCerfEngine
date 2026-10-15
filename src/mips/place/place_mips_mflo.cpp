#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsMflo(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    const int32_t kLoLo = static_cast<int32_t>(offsetof(MipsCpuState, lo));
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, kLoLo);
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rd), kEax);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, kLoLo + 4);
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprHiOff(d->rd), kEax);
    return cursor;
}

}
