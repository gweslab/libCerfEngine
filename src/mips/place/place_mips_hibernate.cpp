#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include <cerfengine/mips/mips_cpu.h>
#include "../mips_emit_services.h"
#include "../../x86_emit.h"

namespace cerfengine {

/* Vr4102-um.pdf ch. 27 HIBERNATE (p. 587): "the internal clocks and the system interface
   clocks will shut down, thus freezing the pipeline". */
uint8_t* PlaceMipsHibernate(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx, d->guest_address + 4u);
    EmitMovRegImm32(cursor, kEdx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Cpu())));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsCpu::HibernateHelper));
    return cursor;
}

}
