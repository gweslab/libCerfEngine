#pragma once

#include <cstdint>

#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/arm/block_context.h>
#include <cerfengine/engine_setup.h>

namespace cerfengine {

enum class ArmCoprocAccessRule : uint8_t { kAccepted, kUndefinedInUser, kUndefined, kCpacr };

inline constexpr const char* kArmCoprocUnknownAction = "has an unknown action";

ArmCoprocAccessRule ArmCoprocAccessRuleFor(const ArmFeatures& features, uint32_t cp_num,
                                           bool user_allowed);

const char* ArmCoprocTransferShapeError(const ArmCoprocDecision& dec, bool read);

[[noreturn]] void ArmCoprocTransferShapePanic(BlockContext* ctx, const DecodedInsn* d,
                                              const char* what);
[[noreturn]] void ArmCoprocDataShapePanic(BlockContext* ctx, const DecodedInsn* d,
                                          const char* kind);
[[noreturn]] void ArmCoprocDoubleShapePanic(BlockContext* ctx, const DecodedInsn* d);

void* ArmTlbInvalidateAllHelperFor(ArmTlbTarget target);
void* ArmTlbInvalidateMvaHelperFor(ArmTlbTarget target);

}
