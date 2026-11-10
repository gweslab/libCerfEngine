#include "dram_host_regions.h"

#include <algorithm>
#include <cstdio>

#include <cerfengine/physical_map.h>
#include <cerfengine/physical_memory_host.h>

namespace cerfengine {

CERFENGINE_REGISTER(DramHostRegions);

DramSpan DramHostRegions::Span(const std::vector<DramRegion>& dram) {
    DramSpan span{0xFFFFFFFFu, 0u};
    for (const DramRegion& r : dram) {
        span.low  = std::min(span.low, r.pa_base);
        span.high = std::max(span.high, r.pa_base + r.size);
    }
    return span;
}

bool DramHostRegions::Validate(const std::vector<DramRegion>& dram, void* memory, std::string& error) {
    char text[160];
    if (dram.size() > DramHostTable::kMaxRegions) {
        std::snprintf(text, sizeof(text),
                      "the host declares %zu cached DRAM regions; the self-modifying-code index supports at most %u",
                      dram.size(), DramHostTable::kMaxRegions);
        error = text;
        return false;
    }
    for (const DramRegion& r : dram) {
        PhysicalPage page;
        if (host::PhysicalTranslate(memory, r.pa_base, false, page) == nullptr || (r.size & 0xFFFu) != 0u) {
            std::snprintf(text, sizeof(text), "DRAM pa=0x%08X size=0x%X is not a page-multiple backed region",
                          r.pa_base, r.size);
            error = text;
            return false;
        }
    }
    return true;
}

void DramHostRegions::OnReady() {
    PhysicalMap&                  map  = engine_.Get<PhysicalMap>();
    const std::vector<DramRegion> dram = engine_.Host().CachedDramRegions();
    for (const DramRegion& r : dram) table_.Add(r, map.PeekRead(r.pa_base));
    span_ = Span(dram);
}

}
