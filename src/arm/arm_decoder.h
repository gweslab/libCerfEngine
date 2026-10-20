#pragma once

#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/engine.h>

#include <cerfengine/force_inline.h>
#include "arm_branch_block_space_decoder.h"
#include "arm_coproc_space_decoder.h"
#include "arm_dataproc_space_decoder.h"
#include "arm_media_space_decoder.h"
#include "arm_opcode.h"
#include "arm_single_data_space_decoder.h"
#include "arm_unconditional_space_decoder.h"

namespace cerfengine {

class ArmDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    /* Table A5-1 (ARM DDI 0406C.c A5.1, p. A5-194): cond + op1 = insn[27:25] +
       op = insn[4] select the instruction class. */
    CERFENGINE_FORCEINLINE bool DecodeArm(DecodedInsn* insn, ArmOpcode op) {
        const uint32_t cond = op.word >> 28;

        if (cond == 0xFu) {
            insn->cond = 14u;
            if (!processor_config_->armv5_unconditional_space) {
                insn->kind = ArmInsnKind::kNop;
                return true;
            }
            return unconditional_decoder_->Decode(insn, op);
        }
        insn->cond = cond;

        switch ((op.word >> 25) & 0x7u) {
        case 0u:
        case 1u:
            /* DDI 0406C.c Table A5-2, p. A5-196. */
            return dataproc_decoder_->Decode(insn, op);
        case 2u:
            /* DDI 0406C.c Table A5-15, p. A5-208. */
            return single_data_decoder_->Decode(insn, op);
        case 3u:
            if (((op.word >> 4) & 0x1u) == 0u) {
                /* DDI 0406C.c Table A5-15, p. A5-208. */
                return single_data_decoder_->Decode(insn, op);
            }
            /* DDI 0406C.c Table A5-16, p. A5-209. */
            return media_decoder_->Decode(insn, op);
        case 4u:
        case 5u:
            /* DDI 0406C.c Table A5-21, p. A5-214. */
            return branch_block_decoder_->Decode(insn, op);
        default:
            /* DDI 0406C.c Table A5-22, p. A5-215. */
            return coproc_decoder_->Decode(insn, op);
        }
    }

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
