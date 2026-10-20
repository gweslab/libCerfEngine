#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct MipsCpuState;

class MipsCpu;
class MipsExceptionDelivery;
class MipsInterruptChannel;
class MipsMemoryAccess;
class MipsMmu;
class MipsTranslationCache;
class MipsWideArithmetic;

class MipsEmitServices : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    MipsCpu*                        Cpu()        { return cpu_; }
    MipsCpuState*                   CpuState()   { return cpu_state_; }
    MipsMmu*                        Mmu()        { return mmu_; }
    const cerfengine::MipsFeatures* CpuConfig()  { return cpu_config_; }

    MipsTranslationCache*  TranslationCache() { return cache_; }
    MipsMemoryAccess*      Memory()           { return memory_access_; }
    MipsWideArithmetic*    WideArithmetic()   { return wide_; }
    MipsExceptionDelivery* Exceptions()       { return exceptions_; }
    MipsInterruptChannel*  InterruptChannel() { return channel_; }

private:
    MipsCpu*                        cpu_         = nullptr;
    MipsCpuState*                   cpu_state_   = nullptr;
    MipsMmu*                        mmu_         = nullptr;
    const cerfengine::MipsFeatures* cpu_config_  = nullptr;

    MipsTranslationCache*  cache_         = nullptr;
    MipsMemoryAccess*      memory_access_ = nullptr;
    MipsWideArithmetic*    wide_          = nullptr;
    MipsExceptionDelivery* exceptions_    = nullptr;
    MipsInterruptChannel*  channel_       = nullptr;
};

}
