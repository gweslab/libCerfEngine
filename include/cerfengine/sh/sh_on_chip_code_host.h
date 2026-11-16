#pragma once

#include <cstdint>

namespace cerfengine {

struct ShCpuState;

class ShOnChipCodeHost {
public:
    virtual ~ShOnChipCodeHost() = default;

    virtual void Call(uint32_t va, ShCpuState& state) = 0;
};

}
