#pragma once

#include <cerfengine/calling_convention.h>
#include <cstddef>
#include <cstdint>

#include <cerfengine/engine.h>
#include "../cerfir/cerfir_guest_model.h"
#include "mips_block_context.h"

namespace cerfengine {

struct MipsCpuState;
struct JitBlock;

class MipsExceptionDelivery;
class MipsInsnDecoder;
class MipsIrCodegen;
class MipsMmu;
class MipsTranslationCache;
class JitBlockIndex;
struct BlockIndexSet;
struct IsaBlockSpace;

class MipsBlockCompiler : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    void* FindBlockNativeStart(uint32_t guest_pc, uint32_t privilege);
    void* Compile(uint32_t guest_pc, uint32_t privilege);
    void* CompilePendingEntry(uint32_t guest_pc, uint32_t privilege);

    void SetPredecessor(uint32_t guest_pc) { predecessor_va_ = guest_pc; }

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
    struct IrIndex {
        IsaBlockSpace* space;
        BlockIndexSet* set;
        JitBlockIndex* idx;
        bool           outer_global;
        uint8_t        asid;
    };

    bool   TailStillMapped(const JitBlock* blk, uint32_t privilege);
    bool   TranslateEntry(uint32_t guest_pc, uint32_t privilege, uint32_t* phys_start);
    bool   DecodeBlock(uint32_t guest_pc, uint32_t privilege);
    void*  EmitIrBlock(uint32_t guest_pc, uint32_t phys_start, const IrIndex* index,
                       uint32_t privilege, bool pending, uint64_t start_ns);
    size_t GenerateIrChecked(uint8_t* code, uint8_t* code_end, bool pending);

    void   FillLink(const IrIndex* index, JitBlock* stored, uint32_t guest_pc,
                    uint32_t phys_start, uint8_t* code, uint32_t privilege);

    MipsBlockContext  block_ctx_{};
    MipsIrCodegen*    ir_codegen_     = nullptr;
    cerfir::BlockLink link_{};
    uint32_t          predecessor_va_ = 0;

    cerfengine::EngineHost* host_      = nullptr;
    bool                   report_compiles_ = false;
    MipsCpuState*          cpu_state_  = nullptr;
    MipsMmu*               mmu_        = nullptr;
    MipsTranslationCache*  cache_      = nullptr;
    MipsInsnDecoder*       decoder_    = nullptr;
    MipsExceptionDelivery* exceptions_ = nullptr;
};

}
