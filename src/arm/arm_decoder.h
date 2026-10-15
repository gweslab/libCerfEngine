#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmBranchBlockSpaceDecoder;
class ArmCoprocSpaceDecoder;
class ArmDataprocSpaceDecoder;
class ArmMediaSpaceDecoder;
class ArmSingleDataSpaceDecoder;
class ArmUnconditionalSpaceDecoder;
struct DecodedInsn;
union  ArmOpcode;

class ArmDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeArm(DecodedInsn* insn, ArmOpcode op);

private:
    const cerfengine::ArmFeatures*           processor_config_      = nullptr;
    ArmBranchBlockSpaceDecoder*   branch_block_decoder_  = nullptr;
    ArmCoprocSpaceDecoder*        coproc_decoder_        = nullptr;
    ArmDataprocSpaceDecoder*      dataproc_decoder_      = nullptr;
    ArmMediaSpaceDecoder*         media_decoder_         = nullptr;
    ArmSingleDataSpaceDecoder*    single_data_decoder_   = nullptr;
    ArmUnconditionalSpaceDecoder* unconditional_decoder_ = nullptr;
};

}
