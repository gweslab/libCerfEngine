#include "arm_decoder.h"

#include <cerfengine/engine.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmDecoder);

bool ArmDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void ArmDecoder::OnReady() {
    processor_config_      = &engine_.GetSetup().arm;
    branch_block_decoder_  = &engine_.Get<ArmBranchBlockSpaceDecoder>();
    coproc_decoder_        = &engine_.Get<ArmCoprocSpaceDecoder>();
    dataproc_decoder_      = &engine_.Get<ArmDataprocSpaceDecoder>();
    media_decoder_         = &engine_.Get<ArmMediaSpaceDecoder>();
    single_data_decoder_   = &engine_.Get<ArmSingleDataSpaceDecoder>();
    unconditional_decoder_ = &engine_.Get<ArmUnconditionalSpaceDecoder>();
}

}
