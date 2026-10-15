#pragma once

#include <cstdint>

namespace cerfengine {

struct DecodedInsn;

class ArmInstructionTiming {
public:
    virtual ~ArmInstructionTiming() = default;

    virtual uint16_t ArmCycleCost(const DecodedInsn& d) const = 0;
};

class ArmUniformInstructionTiming final : public ArmInstructionTiming {
public:
    uint16_t ArmCycleCost(const DecodedInsn&) const override { return 1; }
};

}
