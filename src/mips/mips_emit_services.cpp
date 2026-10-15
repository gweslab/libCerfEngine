#include "mips_emit_services.h"

#include <cerfengine/engine.h>
#include "mips_cp0_emitter.h"
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_exception_delivery.h>
#include <cerfengine/mips/mips_interrupt_channel.h>
#include "mips_memory_access.h"
#include <cerfengine/mips/mips_mmu.h>
#include "mips_translation_cache.h"
#include "mips_wide_arithmetic.h"

namespace cerfengine {

CERFENGINE_REGISTER(MipsEmitServices);

void MipsEmitServices::OnReady() {
    cpu_         = &engine_.Get<MipsCpu>();
    cpu_state_   = cpu_->State();
    mmu_         = &engine_.Get<MipsMmu>();
    cpu_config_  = &engine_.GetSetup().mips;
    cp0_emitter_ = &engine_.Get<MipsCp0Emitter>();

    cache_         = &engine_.Get<MipsTranslationCache>();
    memory_access_ = &engine_.Get<MipsMemoryAccess>();
    wide_          = &engine_.Get<MipsWideArithmetic>();
    exceptions_    = &engine_.Get<MipsExceptionDelivery>();
    channel_       = &engine_.Get<MipsInterruptChannel>();
}

}
