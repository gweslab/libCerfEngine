#pragma once

#include <cstdint>

#include "../calling_convention.h"
#include "arm_mmu_state.h"

namespace cerfengine {

enum class ArmCoprocKind : uint8_t {
    kRegisterTransfer,
    kRegisterTransferDouble,
    kDataTransfer,
    kDataOperation,
};

struct ArmCoprocAccess {
    ArmCoprocKind kind   = ArmCoprocKind::kRegisterTransfer;
    uint32_t      cp_num = 0;
    uint32_t      opc1   = 0;
    uint32_t      crn    = 0;
    uint32_t      crm    = 0;
    uint32_t      opc2   = 0;
    uint32_t      crd    = 0;
    uint32_t      rt     = 0;
    uint32_t      rt2    = 0;
    bool          read   = false;
};

enum class ArmCoprocAction : uint8_t {
    kUndefined,
    kUnimplemented,
    kNoOp,
    kConstant,
    kRegister,
    kRegister40,
    kRegisterContextSwitch,
    kRegisterFlushTlbs,
    kReadCallback,
    kReadCallbackKeyed,
    kWriteCallback,
    kWriteCallbackKeyed,
    kWriteValueSwitch,
    kWaitForInterrupt,
    kSystemControl,
    kCoprocessorAccess,
    kContextId,
    kFcsePid,
    kInvalidateTlbAll,
    kInvalidateTlbByMva,
    kInvalidateCode,
};

enum class ArmTlbTarget : uint8_t { kInstruction, kData, kUnified };

enum class ArmCoprocCase : uint8_t { kContinue, kWaitForInterrupt, kHook };

struct ArmCoprocValueCase {
    uint32_t      value = 0;
    ArmCoprocCase kind  = ArmCoprocCase::kContinue;
};

inline constexpr uint32_t kArmCoprocMaxCases = 4;

using ArmCoprocRead       = uint32_t (CERFENGINE_FASTCALL*)(void* context);
using ArmCoprocReadKeyed  = uint32_t (CERFENGINE_FASTCALL*)(void* context, uint32_t key);
using ArmCoprocWrite      = void (CERFENGINE_FASTCALL*)(void* context, uint32_t value);
using ArmCoprocWriteKeyed = void (CERFENGINE_FASTCALL*)(void* context, uint32_t key,
                                                        uint32_t value);
using ArmCoprocHook       = void (CERFENGINE_FASTCALL*)(void* context);

struct ArmCoprocDecision {
    ArmCoprocAction     action       = ArmCoprocAction::kUndefined;
    bool                user_allowed = false;
    uint32_t            gate_mask    = 0;
    ArmTlbTarget        tlb_target   = ArmTlbTarget::kUnified;
    uint32_t            constant     = 0;
    uint32_t*           reg          = nullptr;
    void*               context      = nullptr;
    uint32_t            key          = 0;
    ArmCoprocRead       read         = nullptr;
    ArmCoprocReadKeyed  read_keyed   = nullptr;
    ArmCoprocWrite      write        = nullptr;
    ArmCoprocWriteKeyed write_keyed  = nullptr;
    ArmCoprocHook       hook         = nullptr;
    uint32_t            case_count   = 0;
    ArmCoprocValueCase  cases[kArmCoprocMaxCases] = {};
};

struct ArmTranslation {
    bool     mapped     = false;
    uint32_t pa         = 0;
    uint32_t span_bytes = 0x1000u;
    bool     global     = false;
    bool     cacheable  = true;
    uint8_t* host       = nullptr;
};

namespace host {

template <ArmMmuAccess kAccess>
ArmTranslation CERFENGINE_FASTCALL ArmWalk(void* context, uint32_t mva, bool user,
                                           uint32_t control);

bool CERFENGINE_FASTCALL ArmProbeWalk(void* context, uint32_t mva, uint32_t control,
                                      uint32_t* pa);
void CERFENGINE_FASTCALL ArmAlignmentFault(void* context, uint32_t va, bool is_write);

}

struct ArmResetResult {
    bool     resume_mmu       = false;
    uint32_t control_register = 0;
};

class ArmSystemHost {
public:
    virtual ~ArmSystemHost() = default;

    virtual ArmCoprocDecision ArmCoprocessor(const ArmCoprocAccess& access) = 0;
    virtual ArmResetResult    ArmReset()                                    = 0;
};

}
