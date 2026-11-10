#pragma once

#include <cstdint>

namespace cerfengine {

class ShInterruptHost {
public:
    virtual ~ShInterruptHost() = default;

    virtual uint32_t AcceptInterrupt(uint32_t sr) = 0;
    virtual uint32_t StandbyWakeLevel()          = 0;
};

}
