#pragma once

#include <cerfengine/calling_convention.h>
#include <cstddef>
#include <cstdint>

#include <cerfengine/engine.h>
#include "mips_block_context.h"

namespace cerfengine {

struct MipsCpuState;
struct JitBlock;

class MipsExceptionDelivery;
class MipsInsnDecoder;
class MipsMmu;
class MipsTranslationCache;

class MipsBlockCompiler : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    void* FindBlockNativeStart(uint32_t guest_pc);
    void* Compile(uint32_t guest_pc);

    static void CERFENGINE_CDECL UnimplementedHelper(MipsBlockCompiler* c, uint32_t pc,
                                                     uint32_t raw);

    static void CERFENGINE_CDECL PcrelDelaySlotHelper(MipsBlockCompiler* c, uint32_t pc);

    static void CERFENGINE_CDECL TraceDispatchPcHelper(MipsBlockCompiler* c, uint32_t pc);

    /* QEMU MIPS gen_branch (translate.c). */
    static int CERFENGINE_FASTCALL ResolveBranchHelper(uint32_t fallthrough,
                                                       MipsBlockCompiler* c);

    /* QEMU MIPS decode_opc "blikely not taken" (translate.c). */
    static int CERFENGINE_FASTCALL NullifyLikelyHelper(uint32_t fallthrough,
                                                       MipsBlockCompiler* c);

private:
    bool   TailStillMapped(const JitBlock* blk);
    size_t GenerateCode(uint8_t* code_location);
    size_t GenerateCodeChecked(uint8_t* code_location);

    MipsBlockContext block_ctx_{};

    cerfengine::EngineHost* host_      = nullptr;
    bool                   report_compiles_ = false;
    MipsCpuState*          cpu_state_  = nullptr;
    MipsMmu*               mmu_        = nullptr;
    MipsTranslationCache*  cache_      = nullptr;
    MipsInsnDecoder*       decoder_    = nullptr;
    MipsExceptionDelivery* exceptions_ = nullptr;
};

}
