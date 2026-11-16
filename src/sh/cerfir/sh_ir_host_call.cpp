#include "sh_ir_fns.h"

namespace cerfengine {

namespace {

constexpr uint32_t kDelaySlotOffset = 2u;

}

uint32_t ShIrRestartPc(const ShIrContext& c) {
    return c.in_delay_slot ? c.d->guest_address - kDelaySlotOffset : c.d->guest_address;
}

cerfir::CallDesc ShIrHostCall(const void* fn, uint8_t num_args) {
    return cerfir::CallDesc{fn, cerfir::CallConv::kCdecl, num_args};
}

cerfir::CallSpec ShIrHostCallConst(const void* fn) {
    return cerfir::CallSpec{fn, cerfir::CallConv::kCdecl, cerfir::Type::kVoid};
}

}
