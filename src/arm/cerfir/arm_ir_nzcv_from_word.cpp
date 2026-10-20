#include <cerfengine/arm/arm_cpu.h>

#include "../arm_emit_services.h"
#include "arm_ir_fns.h"

namespace cerfengine {

void ArmIrSetNzcvFromWord(ArmIrContext& c, cerfir::Ref value) {
    const cerfir::CallDesc nzcv{reinterpret_cast<const void*>(&ArmCpu::UpdateNzcvOnlyHelper),
                                cerfir::CallConv::kCdecl, 2u};
    c.b->Call(nzcv, cerfir::Type::kVoid, c.b->ConstPtr(c.block->emit->Cpu()), value,
              cerfir::kNoRef);
}

}
