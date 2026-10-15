#pragma once

#include <atomic>
#include <cstdint>

#include "byte_order.h"
#include "calling_convention.h"

namespace cerfengine {

enum class MmioWidth : uint32_t { kByte = 1u, kHalf = 2u, kWord = 4u, kDword = 8u };

/* ARM DDI 0406C.c A8.8.68 LDRB (p. A8-418), A8.8.80 LDRH (p. A8-442): a byte
   or halfword load "zero-extends it to form a 32-bit word". */
inline uint32_t ClipToWidth(uint32_t value, MmioWidth width) {
    return value & ByteWidthMask(static_cast<size_t>(width));
}

using MmioReadFn  = uint32_t (CERFENGINE_CDECL*)(void* context, uint32_t offset,
                                                 uint32_t width_bytes);
using MmioWriteFn = void (CERFENGINE_CDECL*)(void* context, uint32_t offset, uint32_t value,
                                             uint32_t width_bytes);

struct MmioRoute {
    uint32_t    base    = 1u;
    uint32_t    last    = 0u;
    MmioReadFn  read    = nullptr;
    MmioWriteFn write   = nullptr;
    void*       context = nullptr;

    uint32_t Read(uint32_t pa, MmioWidth width) const {
        return ClipToWidth(read(context, pa - base, static_cast<uint32_t>(width)), width);
    }

    void Write(uint32_t pa, uint32_t value, MmioWidth width) const {
        write(context, pa - base, ClipToWidth(value, width), static_cast<uint32_t>(width));
    }
};

struct MmioBusBinding {
    void*                        context    = nullptr;
    const std::atomic<uint32_t>* generation = nullptr;
};

namespace host {

bool CERFENGINE_FASTCALL     MmioFindRoute(void* context, uint32_t pa, MmioRoute& route);
uint32_t CERFENGINE_FASTCALL MmioReadSlow(void* context, uint32_t pa, MmioWidth width,
                                          MmioRoute& route);
void CERFENGINE_FASTCALL     MmioWriteSlow(void* context, uint32_t pa, uint32_t value,
                                           MmioWidth width, MmioRoute& route);
uint64_t CERFENGINE_FASTCALL MmioReadDword(void* context, uint32_t pa);
void CERFENGINE_FASTCALL     MmioWriteDword(void* context, uint32_t pa, uint64_t value);

}

}
