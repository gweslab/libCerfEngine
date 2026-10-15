#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"
#include "../mips_emit_services.h"
#include "../mips_memory_access.h"
#include "../../x86_emit.h"

namespace cerfengine {

/* MIPS16 LD ry, offset(pc) (U15509EJ2V0UM Table 3-14 p72): EA = BasePC with
   the lower three bits cleared + imm<<3; full 64-bit load. ry encodes $2-$7/$16/$17 (Table 3-1 p55), never r0. */
uint8_t* PlaceMips16Ldpc(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx, d->target);
    EmitMovRegImm32(cursor, kEdx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Memory())));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsMemoryAccess::LoadDwordHelper));
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rt), kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprHiOff(d->rt), kEdx);
    return cursor;
}

}
