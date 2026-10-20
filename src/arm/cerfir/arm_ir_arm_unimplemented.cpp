#include "../arm_emit_services.h"
#include "../arm_unimplemented_halt.h"
#include "arm_ir_fns.h"

namespace cerfengine {

void ArmIrUnimplementedHalt(ArmIrContext& c, const void* helper, uint32_t info) {
    const cerfir::CallSpec halt{helper, cerfir::CallConv::kCdecl, cerfir::Type::kVoid};
    c.b->CallConst(halt, 4u, cerfir::CallHostAddress(c.block->emit->UnimplementedHalt()),
                   c.d->guest_address, info, c.block->thumb ? 1u : 0u);
    c.b->ExitDispatcher();
}

void ArmIrArmUnimplemented(ArmIrContext& c) {
    ArmIrUnimplementedHalt(c, reinterpret_cast<const void*>(&ArmUnimplementedHalt::InstructionHelper),
                           c.d->immediate);
}

}
