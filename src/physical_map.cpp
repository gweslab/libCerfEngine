#include <cerfengine/physical_map.h>

#include <algorithm>

namespace cerfengine {

CERFENGINE_REGISTER(PhysicalMap);

void PhysicalMap::OnReady() {
    host_ = engine_.GetSetup().memory;
}

uint8_t* PhysicalMap::Miss(uint32_t pa, bool write) {
    PhysicalPage   page;
    uint8_t* const byte = host::PhysicalTranslate(host_, pa, write, page);
    if (page.read == nullptr) return byte;

    uint8_t* const expected = write ? page.write : page.read;
    if (byte != (expected != nullptr ? expected + (pa & kPageOffsetMask) : nullptr)) {
        engine_.Panic("PhysicalMap: the host maps PA 0x%08X to %p, but its page to %p",
                      pa, static_cast<void*>(byte), static_cast<void*>(expected));
    }
    std::unique_ptr<PhysicalPage[]>& table = tables_[pa >> kTableShift];
    if (!table) table = std::make_unique<PhysicalPage[]>(kPagesPerTable);
    table[(pa >> kPageShift) & kPageIndexMask] = page;
    return byte;
}

uint8_t* PhysicalMap::PeekRead(uint32_t pa) const {
    PhysicalPage page;
    return host::PhysicalTranslate(host_, pa, false, page);
}

uint8_t* PhysicalMap::PeekWrite(uint32_t pa) const {
    PhysicalPage page;
    return host::PhysicalTranslate(host_, pa, true, page);
}

void PhysicalMap::Invalidate() {
    for (std::unique_ptr<PhysicalPage[]>& table : tables_) {
        if (table) std::fill_n(table.get(), kPagesPerTable, PhysicalPage{});
    }
}

}
