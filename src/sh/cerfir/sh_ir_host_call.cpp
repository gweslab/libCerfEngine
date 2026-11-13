#include "sh_ir_fns.h"

namespace cerfengine {

cerfir::CallDesc ShIrHostCall(const ShIrContext& c, const void* fn, uint8_t num_args) {
    cerfir::CallDesc call{fn, cerfir::CallConv::kCdecl, num_args};
    call.store_pc = true;
    call.pc       = c.d->guest_address;
    return call;
}

cerfir::CallSpec ShIrHostCallConst(const void* fn, uint32_t guest_pc) {
    cerfir::CallSpec spec{fn, cerfir::CallConv::kCdecl, cerfir::Type::kVoid};
    spec.store_pc = true;
    spec.pc       = guest_pc;
    return spec;
}

}
