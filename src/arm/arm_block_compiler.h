#pragma once

#include <cerfengine/calling_convention.h>
#include <cstddef>
#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_instruction_timing.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

struct ArmCpuState;

class ArmCpu;
class ArmDecoder;
class ArmEmitServices;
class ArmMmu;
class ThumbDecoder;
class Thumb32Decoder;
class ArmRoutedInstruction;
class ArmTranslationCache;
class ArmTranslator;

class ArmBlockCompiler : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kArm;
    }

    void* Compile(uint32_t guest_pc);

    void SetPredecessor(uint32_t folded_va) { predecessor_va_ = folded_va; }

    static void CERFENGINE_FASTCALL SctlrWriteHelper(uint32_t value,
                                                     ArmBlockCompiler* compiler);
    static void CERFENGINE_FASTCALL RaiseAbortDataHelper(uint32_t guest_pc,
                                                         ArmBlockCompiler* compiler);
    static void CERFENGINE_CDECL TraceDispatchPcHelper(ArmBlockCompiler* compiler,
                                                       uint32_t pc);

private:
    uint32_t predecessor_va_ = 0;
    bool     thumb_          = false;
    bool     fetch_fault_    = false;
    uint32_t fetch_fault_pc_ = 0;
    uint32_t wide_split_     = 0;
    uint32_t wide_pa2_       = 0;
    uint32_t tail_itstate_       = 0;
    bool     tail_itstate_valid_ = false;

    void   BuildTrampolines();
    void   Decode(uint32_t guest_pc, uint32_t folded_pc);
    size_t GenerateCode(uint8_t* code, uint8_t* code_end);
    size_t GenerateCodeChecked(uint8_t* code, uint8_t* code_end);

    BlockContext block_ctx_{};

    ArmTranslationCache*    cache_     = nullptr;
    ArmEmitServices*        emit_      = nullptr;
    ArmCpu*                 cpu_       = nullptr;
    ArmCpuState*            cpu_state_ = nullptr;
    ArmDecoder*             decoder_   = nullptr;
    ThumbDecoder*           thumb_decoder_ = nullptr;
    Thumb32Decoder*         thumb32_decoder_ = nullptr;
    ArmMmu*                 mmu_       = nullptr;
    ArmTranslator*          translator_ = nullptr;
    ArmRoutedInstruction*   routed_    = nullptr;
    const cerfengine::ArmInstructionTiming* timing_ = nullptr;
    cerfengine::EngineHost* host_      = nullptr;
    bool                    report_compiles_ = false;

    cerfengine::ArmUniformInstructionTiming uniform_timing_;
};

}
