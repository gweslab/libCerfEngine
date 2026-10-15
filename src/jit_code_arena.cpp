#include "jit_code_arena.h"

#include <cerfengine/engine.h>
#include "host_platform.h"

namespace cerfengine {

JitCodeArena::~JitCodeArena() {
    if (region_start_) {
        cerfengine::platform::FreeExecutable(region_start_);
    }
}

void JitCodeArena::Initialize(cerfengine::Engine& engine) {
    if (region_start_) {
        engine.Panic("JitCodeArena::Initialize called twice");
    }
    engine_ = &engine;
    void* region = cerfengine::platform::AllocateExecutable(kRegionSize);
    if (!region) {
        engine.Panic("JitCodeArena::Initialize: allocating %zu executable bytes failed",
                     kRegionSize);
    }
    region_start_ = static_cast<uint8_t*>(region);
    flush_floor_  = region_start_;
    cursor_       = region_start_;
    region_end_   = region_start_ + kRegionSize;
    region_size_  = kRegionSize;
}

uint8_t* JitCodeArena::Allocate(size_t size) {
    const size_t aligned = (size + 3u) & ~static_cast<size_t>(3u);
    if (aligned < size) {
        engine_->Panic("JitCodeArena::Allocate: size overflow (%zu)", size);
    }
    if (static_cast<size_t>(region_end_ - cursor_) < aligned) {
        return nullptr;
    }
    uint8_t* p = cursor_;
    cursor_ += aligned;
    return p;
}

void JitCodeArena::FreeUnusedTail(uint8_t* start_of_free) {
    if (start_of_free < flush_floor_ || start_of_free > cursor_) {
        engine_->Panic("JitCodeArena::FreeUnusedTail: %p outside [%p, %p]",
                       start_of_free, flush_floor_, cursor_);
    }
    cursor_ = reinterpret_cast<uint8_t*>(
        (reinterpret_cast<uintptr_t>(start_of_free) + 3u) &
        ~static_cast<uintptr_t>(3u));
}

uint8_t* JitCodeArena::AllocatePermanent(size_t size) {
    uint8_t* p = Allocate(size);
    flush_floor_ = cursor_;
    return p;
}

void JitCodeArena::Flush() {
    cursor_ = flush_floor_;
}

}
