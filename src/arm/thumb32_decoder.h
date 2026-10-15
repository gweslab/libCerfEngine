#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

class ArmCoprocSpaceDecoder;
class NeonUnconditionalDecoder;
class Thumb32BranchSystemDecoder;
class Thumb32DataProcDecoder;
class Thumb32Fatal;
class Thumb32LoadByteDecoder;
class Thumb32LoadHalfwordDecoder;
class Thumb32LoadStoreDecoder;
class Thumb32LoadStoreDualDecoder;
class Thumb32LoadStoreMultipleDecoder;
class Thumb32LongMultiplyDecoder;
class Thumb32MultiplyDecoder;
class Thumb32PlainImmDecoder;
struct DecodedInsn;

class Thumb32Decoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool IsWide(uint16_t first_halfword) const {
        const uint32_t top5 = (first_halfword >> 11) & 0x1Fu;
        return has_thumb2_ &&
               (top5 == 0x1Du || top5 == 0x1Eu || top5 == 0x1Fu);
    }

    bool DecodeThumb32(DecodedInsn* insn, uint32_t op);

private:
    bool has_thumb2_ = false;
    bool has_neon_   = false;

    ArmCoprocSpaceDecoder*      coproc_decoder_ = nullptr;
    NeonUnconditionalDecoder*   neon_decoder_   = nullptr;
    Thumb32BranchSystemDecoder* branch_system_  = nullptr;
    Thumb32DataProcDecoder*     data_proc_      = nullptr;
    Thumb32Fatal*               fatal_          = nullptr;
    Thumb32LoadByteDecoder*     load_byte_       = nullptr;
    Thumb32LoadHalfwordDecoder* load_halfword_   = nullptr;
    Thumb32LoadStoreDecoder*    load_store_      = nullptr;
    Thumb32LoadStoreDualDecoder* load_store_dual_ = nullptr;
    Thumb32LongMultiplyDecoder* long_multiply_   = nullptr;
    Thumb32MultiplyDecoder*     multiply_        = nullptr;
    Thumb32PlainImmDecoder*     plain_imm_       = nullptr;

    Thumb32LoadStoreMultipleDecoder* load_store_multiple_ = nullptr;

    bool DecodeCoprocessorSimdFp(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadByteMemoryHints(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadHalfwordMemoryHints(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadStoreDualExclusiveTableBranch(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadStoreMultiple(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadWord(DecodedInsn* insn, uint32_t op);
    bool DecodeLongMultiplyDivide(DecodedInsn* insn, uint32_t op);
    bool DecodeMultiplyAbsoluteDifference(DecodedInsn* insn, uint32_t op);
    bool DecodeSimdElementOrStructure(DecodedInsn* insn, uint32_t op);
    bool DecodeStoreSingleDataItem(DecodedInsn* insn, uint32_t op);
};

}
