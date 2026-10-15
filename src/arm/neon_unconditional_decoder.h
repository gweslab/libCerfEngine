#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmNeon2RegUnaryDecoder;
class ArmNeon2RegScalarDecoder;
class ArmNeon3DiffLenDecoder;
class ArmNeon3RegSameDecoder;
class ArmNeonLoadStoreDecoder;
class ArmNeonShiftImmDecoder;
struct DecodedInsn;
union  ArmOpcode;

class NeonUnconditionalDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeLoadStore(DecodedInsn* insn, ArmOpcode op);
    bool DecodeData3reg(DecodedInsn* insn, ArmOpcode op);

private:
    ArmNeonLoadStoreDecoder*   loadstore_decoder_      = nullptr;
    ArmNeon3RegSameDecoder*    three_regsame_decoder_  = nullptr;
    ArmNeonShiftImmDecoder*    shift_imm_decoder_      = nullptr;
    ArmNeon3DiffLenDecoder*    three_difflen_decoder_  = nullptr;
    ArmNeon2RegScalarDecoder*  two_regscalar_decoder_  = nullptr;
    ArmNeon2RegUnaryDecoder*   two_reg_unary_decoder_  = nullptr;
};

}
