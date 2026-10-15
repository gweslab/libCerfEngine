#include "mmio_bus.h"

namespace cerfengine {

CERFENGINE_REGISTER(MmioBus);

void MmioBus::OnReady() {
    host_       = engine_.GetSetup().mmio;
    generation_ = host_.generation;
}

const MmioRoute* MmioBus::RouteSlow(uint32_t pa) {
    const uint32_t generation = generation_->load(std::memory_order_acquire);
    if (generation != memo_generation_) Forget(generation);
    return host::MmioFindRoute(host_.context, pa, memo_) ? &memo_ : nullptr;
}

}
