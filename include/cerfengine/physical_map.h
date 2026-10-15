#pragma once

#include <cstdint>
#include <memory>

#include "engine.h"
#include "physical_memory_host.h"

namespace cerfengine {

class PhysicalMap : public EngineService {
public:
    using EngineService::EngineService;

    void OnReady() override;

    uint8_t* Read(uint32_t pa) {
        if (const PhysicalPage* table = tables_[pa >> kTableShift].get()) {
            const PhysicalPage& page = table[(pa >> kPageShift) & kPageIndexMask];
            if (page.read != nullptr) return page.read + (pa & kPageOffsetMask);
        }
        return Miss(pa, false);
    }

    uint8_t* Write(uint32_t pa) {
        if (const PhysicalPage* table = tables_[pa >> kTableShift].get()) {
            const PhysicalPage& page = table[(pa >> kPageShift) & kPageIndexMask];
            if (page.read != nullptr) {
                return page.write != nullptr ? page.write + (pa & kPageOffsetMask) : nullptr;
            }
        }
        return Miss(pa, true);
    }

    uint8_t* PeekRead(uint32_t pa) const;
    uint8_t* PeekWrite(uint32_t pa) const;

    void Invalidate();

private:
    static constexpr uint32_t kPageShift      = 12u;
    static constexpr uint32_t kTableShift     = 22u;
    static constexpr uint32_t kTableCount     = 1u << (32u - kTableShift);
    static constexpr uint32_t kPagesPerTable  = 1u << (kTableShift - kPageShift);
    static constexpr uint32_t kPageIndexMask  = kPagesPerTable - 1u;
    static constexpr uint32_t kPageOffsetMask = (1u << kPageShift) - 1u;

    uint8_t* Miss(uint32_t pa, bool write);

    void*                           host_ = nullptr;
    std::unique_ptr<PhysicalPage[]> tables_[kTableCount];
};

}
