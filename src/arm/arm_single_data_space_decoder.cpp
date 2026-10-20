#include "arm_single_data_space_decoder.h"

#include <cerfengine/engine.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmSingleDataSpaceDecoder);

bool ArmSingleDataSpaceDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

}
