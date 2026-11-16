#pragma once

#include <cstdint>

namespace cerfengine::cerfir {

struct SoftTlbMissSites {
    static constexpr uint32_t kMax = 3;

    uint8_t* site[kMax];
    uint32_t count;
};

}
