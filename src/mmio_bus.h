#pragma once

#include <atomic>
#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/mmio_bus_host.h>

namespace cerfengine {

class MmioBus : public EngineService {
public:
    using EngineService::EngineService;

    void OnReady() override;

    const MmioRoute* Route(uint32_t pa) {
        if (generation_->load(std::memory_order_acquire) == memo_generation_ &&
            pa >= memo_.base && pa <= memo_.last) {
            return &memo_;
        }
        return RouteSlow(pa);
    }

    uint32_t Read(uint32_t pa, MmioWidth width) {
        const uint32_t generation = generation_->load(std::memory_order_acquire);
        if (generation != memo_generation_) {
            Forget(generation);
        } else if (pa >= memo_.base && pa <= memo_.last) {
            return memo_.Read(pa, width);
        }
        return host::MmioReadSlow(host_.context, pa, width, memo_);
    }

    void Write(uint32_t pa, uint32_t value, MmioWidth width) {
        const uint32_t generation = generation_->load(std::memory_order_acquire);
        if (generation != memo_generation_) {
            Forget(generation);
        } else if (pa >= memo_.base && pa <= memo_.last) {
            memo_.Write(pa, value, width);
            return;
        }
        host::MmioWriteSlow(host_.context, pa, value, width, memo_);
    }

private:
    void Forget(uint32_t generation) {
        memo_.base       = 1u;
        memo_.last       = 0u;
        memo_generation_ = generation;
    }

    const MmioRoute* RouteSlow(uint32_t pa);

    MmioBusBinding               host_;
    const std::atomic<uint32_t>* generation_      = nullptr;
    uint32_t                     memo_generation_ = 0;
    MmioRoute                    memo_;
};

}
