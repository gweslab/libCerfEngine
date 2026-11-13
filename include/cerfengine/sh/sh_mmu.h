#pragma once

#include <cstdint>

#include "../calling_convention.h"
#include "../engine.h"
#include "sh_cpu_state.h"

namespace cerfengine {

struct IsaBlockSpace;
class ShCpu;

enum class ShAccess : uint8_t { kRead, kWrite, kFetch };

struct ShTranslation {
    uint32_t pa;
    bool     global;
};

class ShMmu : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }
    void OnReady() override;

    void Reset(ShResetKind kind);

    uint32_t ReadMmucr();
    void     WriteMmucr(uint32_t value);
    uint32_t ReadPteh();
    void     WritePteh(uint32_t value);
    uint32_t ReadPtel();
    void     WritePtel(uint32_t value);
    uint32_t ReadTtb();
    void     WriteTtb(uint32_t value);
    uint32_t ReadTea();
    void     WriteTea(uint32_t value);

    bool AddressTranslation() const;
    bool Translates(uint32_t va) const;
    bool Translate(uint32_t va, ShAccess access, uint32_t exception_pc, ShTranslation* out);
    bool Probe(uint32_t va, ShAccess access, ShTranslation* out);
    bool ProbeFetch(uint32_t va, ShTranslation* out);
    void RaiseAddressError(uint32_t va, ShAccess access, uint32_t exception_pc);
    uint8_t Asid() const { return static_cast<uint8_t>(pteh_ & kAsidMask); }

    void LoadTlb();
    static void CERFENGINE_CDECL LoadTlbHelper(ShMmu* mmu);
    static void CERFENGINE_CDECL ReturnAfterLdtlbHelper(ShMmu* mmu);

private:
    static constexpr uint32_t kWays     = 4u;
    static constexpr uint32_t kSets     = 32u;
    static constexpr uint32_t kAsidMask = 0xFFu;

    struct Entry {
        uint32_t vpn     = 0;
        uint32_t data    = 0;
        uint8_t  asid    = 0;
        bool     valid   = false;
        bool     defined = false;
    };

    enum class Fault : uint8_t { kNone, kMiss, kInvalid, kProtection, kInitialWrite, kMultiHit };

    uint32_t SetIndex(uint32_t vpn, uint8_t asid) const;
    bool     Matches(const Entry& e, uint32_t va) const;
    Fault    Lookup(uint32_t va, ShAccess access, ShTranslation* out, uint32_t* way);
    void     Raise(Fault fault, uint32_t va, ShAccess access, uint32_t way, uint32_t exception_pc);
    void     LatchFaultAddress(uint32_t va);
    void     ClearPage(const Entry& e);
    uint32_t RequireDefined(uint32_t mask, const char* name, uint32_t value);
    void     RequireUntranslatedPc(const char* name, uint32_t value);

    Entry          tlb_[kSets][kWays]{};
    uint32_t       mmucr_   = 0;
    uint32_t       pteh_    = 0;
    uint32_t       ptel_    = 0;
    uint32_t       ttb_     = 0;
    uint32_t       tea_     = 0;
    uint32_t       defined_ = 0;
    ShCpu*         cpu_     = nullptr;
    ShCpuState*    state_   = nullptr;
    IsaBlockSpace* space_   = nullptr;
};

}
