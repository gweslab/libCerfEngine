#pragma once

#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/sh/sh_mmu.h>

#include "../cerfir/cerfir_guest_model.h"
#include "sh_decoded_insn.h"

namespace cerfengine {

class JitBlockEmitter;
class PhysicalMap;
struct JitBlock;
class ShDecoder;
class ShIrCodegen;
class ShTranslationCache;
struct ShCpuState;

class ShBlockCompiler : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }
    void OnReady() override;

    void* FindBlockNativeStart(uint32_t pc);
    void* Compile(uint32_t pc);
    void  SetPredecessor(uint32_t va) { predecessor_va_ = va; }

private:
    static constexpr uint32_t kMaxBlockInsns   = 64;
    static constexpr uint32_t kPageBytes       = 0x400;
    static constexpr uint32_t kLdtlbBlockInsns = kLdtlbShadowInsns + 2u;

    uint32_t ProcessorMode() const;
    uint32_t MemoryMode(uint32_t mode, uint32_t count) const;
    bool     TranslateFetch(uint32_t va, bool deliver, uint32_t exception_pc, ShTranslation* out);
    bool     TailStillMapped(const JitBlock* blk);
    uint32_t DecodeBlock(uint32_t pc, uint32_t phys_start);

    ShDecodedInsn       insns_[kMaxBlockInsns]{};
    cerfir::BlockLink   link_{};
    uint32_t            predecessor_va_ = 0;
    uint32_t            tail_split_     = 0;
    uint32_t            tail_page_pa_   = 0;
    bool                report_compiles_ = false;
    const ShCpuState*   cpu_state_      = nullptr;
    PhysicalMap*        map_            = nullptr;
    ShTranslationCache* cache_          = nullptr;
    ShIrCodegen*        codegen_        = nullptr;
    ShDecoder*          decoder_        = nullptr;
    JitBlockEmitter*    emitter_        = nullptr;
    ShMmu*              mmu_            = nullptr;
};

}
