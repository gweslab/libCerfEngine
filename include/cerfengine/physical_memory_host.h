#pragma once

#include <cstdint>

#include "calling_convention.h"

namespace cerfengine {

struct PhysicalPage {
    uint8_t* read  = nullptr;
    uint8_t* write = nullptr;
};

namespace host {

uint8_t* CERFENGINE_FASTCALL PhysicalTranslate(void* context, uint32_t pa, bool write,
                                               PhysicalPage& page);
bool CERFENGINE_FASTCALL PhysicalSpanUniform(void* context, uint32_t pa, uint32_t span);

}

}
