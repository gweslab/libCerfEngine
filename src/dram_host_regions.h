#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <cerfengine/engine.h>

namespace cerfengine {

struct DramSpan {
    uint32_t low;
    uint32_t high;
};

class DramHostTable {
public:
    static constexpr uint32_t kNotDram    = 0xFFFFFFFFu;
    static constexpr uint32_t kMaxRegions = 4u;

    void Add(const DramRegion& region, const uint8_t* host) {
        regions_[count_++] = Region{host, host + region.size, region.pa_base, packed_bytes_};
        packed_bytes_ += region.size;
    }

    uint32_t Count() const { return count_; }
    uint32_t PackedBytes() const { return packed_bytes_; }

    uint32_t PackedOffset(const uint8_t* host) const {
        const Region* r = Find(host);
        return r != nullptr ? r->packed_base + static_cast<uint32_t>(host - r->host_lo) : kNotDram;
    }

    uint32_t CanonicalPa(const uint8_t* host) const {
        const Region* r = Find(host);
        return r != nullptr ? r->pa_base + static_cast<uint32_t>(host - r->host_lo) : kNotDram;
    }

private:
    struct Region {
        const uint8_t* host_lo     = nullptr;
        const uint8_t* host_hi     = nullptr;
        uint32_t       pa_base     = 0;
        uint32_t       packed_base = 0;
    };

    const Region* Find(const uint8_t* host) const {
        for (uint32_t i = 0; i < count_; ++i) {
            if (host >= regions_[i].host_lo && host < regions_[i].host_hi) return &regions_[i];
        }
        return nullptr;
    }

    Region   regions_[kMaxRegions]{};
    uint32_t count_        = 0;
    uint32_t packed_bytes_ = 0;
};

class DramHostRegions : public EngineService {
public:
    using EngineService::EngineService;

    static bool     Serves(Isa isa) { return isa == Isa::kMips || isa == Isa::kSh; }
    static DramSpan Span(const std::vector<DramRegion>& dram);
    static bool     Validate(const std::vector<DramRegion>& dram, void* memory, std::string& error);

    bool ShouldRegister() override { return Serves(engine_.GetSetup().isa); }
    void OnReady() override;

    const DramHostTable& Table() const { return table_; }
    DramSpan             PaSpan() const { return span_; }

private:
    DramHostTable table_;
    DramSpan      span_{0xFFFFFFFFu, 0u};
};

}
