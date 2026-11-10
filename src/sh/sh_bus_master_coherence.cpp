#include <cerfengine/sh/sh_bus_master_coherence.h>

#include <cerfengine/physical_map.h>
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include "sh_translation_cache.h"

namespace cerfengine {

CERFENGINE_REGISTER(ShBusMasterCoherence);

void ShBusMasterCoherence::OnReady() {
    cpu_state_    = engine_.Get<ShCpu>().State();
    translations_ = &engine_.Get<ShTranslationCache>();
    map_          = &engine_.Get<PhysicalMap>();
}

void ShBusMasterCoherence::AfterWrite(uint32_t pa, uint32_t bytes) {
    if (const uint8_t* host = map_->Write(pa)) {
        translations_->InvalidateBusMasterWrite(host, bytes, cpu_state_->mmio_access != 0u);
    }
}

}
