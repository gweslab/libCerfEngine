#pragma once

#include <cstdint>

#include "calling_convention.h"

namespace cerfengine {

class ClockHost {
public:
    virtual ~ClockHost() = default;

    virtual void     IdleStep(void* idle_event) = 0;
    virtual void     WaitStep(void* idle_event) = 0;
    virtual void     ExitIdle()                 = 0;
    virtual uint64_t Cycles()                   = 0;
};

namespace host {

void CERFENGINE_FASTCALL ClockDispatch(ClockHost* clock);

}

}
