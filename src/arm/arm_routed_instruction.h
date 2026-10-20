#pragma once

#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmCpu;
class ArmDecoder;
class ArmMmu;
class ArmRoutedAccess;
class ArmRoutedAddressing;
class ArmRoutedTableBranch;
class Thumb32Decoder;
class ThumbDecoder;
struct ArmCpuState;
struct DecodedInsn;

class ArmRoutedInstruction : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    void Complete(uint32_t guest_pc, uint32_t raw);
    void CompleteOrRaiseAbort(uint32_t guest_pc, uint32_t raw);

private:
    enum class Outcome { kNextInsn, kPcWritten, kExceptionEntered, kAborted };

    Outcome SingleTransfer(DecodedInsn* d);
    Outcome HalfwordTransfer(DecodedInsn* d);
    Outcome BlockTransfer(DecodedInsn* d);
    Outcome Swap(DecodedInsn* d);
    Outcome Exclusive(DecodedInsn* d, bool is_store);

    Outcome  Abort(DecodedInsn* d, bool wback, uint32_t base_on_abort);
    void     LoadWritePc(uint32_t value);

    ArmCpu*                   cpu_       = nullptr;
    ArmCpuState*              cpu_state_ = nullptr;
    ArmDecoder*               decoder_   = nullptr;
    ArmMmu*                   mmu_       = nullptr;
    const cerfengine::ArmFeatures*       config_    = nullptr;
    ArmRoutedAccess*          access_     = nullptr;
    ArmRoutedAddressing*      addressing_ = nullptr;
    ThumbDecoder*             thumb_     = nullptr;
    Thumb32Decoder*           thumb32_   = nullptr;
    ArmRoutedTableBranch*     table_branch_ = nullptr;
};

}
