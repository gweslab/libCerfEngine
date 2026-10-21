#pragma once

#include <cstdint>

namespace cerfengine {

inline void* WakeEventHandle(int descriptor) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(descriptor) + 1);
}

inline int WakeEventDescriptor(void* handle) {
    return static_cast<int>(reinterpret_cast<intptr_t>(handle) - 1);
}

}
