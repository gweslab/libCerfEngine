#include <cerfengine/sh/sh_mmu.h>

#include <cerfengine/isa_block_space.h>
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include "sh_address_space.h"
#include "sh_translation_cache.h"

namespace cerfengine {

CERFENGINE_REGISTER(ShMmu);

namespace {

constexpr uint32_t kMmucrAt       = 1u << 0;
constexpr uint32_t kMmucrIx       = 1u << 1;
constexpr uint32_t kMmucrTf       = 1u << 2;
constexpr uint32_t kMmucrRcShift  = 4u;
constexpr uint32_t kMmucrRc       = 3u << kMmucrRcShift;
constexpr uint32_t kMmucrSv       = 1u << 8;
constexpr uint32_t kMmucrStored   = kMmucrSv | kMmucrRc | kMmucrIx | kMmucrAt;
constexpr uint32_t kMmucrMapping  = kMmucrSv | kMmucrIx | kMmucrAt;

constexpr uint32_t kPtehWritable  = 0xFFFFFCFFu;
constexpr uint32_t kPtelWritable  = 0xFFFFFD7Eu;
constexpr uint32_t kVpnMask       = 0xFFFFFC00u;

constexpr uint32_t kPtelV         = 1u << 8;
constexpr uint32_t kDataStored    = 0xFFFFFC7Eu;
constexpr uint32_t kDataPrShift   = 5u;
constexpr uint32_t kDataSz        = 1u << 4;
constexpr uint32_t kDataD         = 1u << 2;
constexpr uint32_t kDataSh        = 1u << 1;

constexpr uint32_t kVpnHigh       = 0xFFFE0000u;
constexpr uint32_t kVpnIndex      = 0x0001F000u;
constexpr uint32_t kVpn1k         = 0x00000C00u;
constexpr uint32_t kIndexShift    = 12u;
constexpr uint32_t kIndexMask     = 0x1Fu;
constexpr uint32_t kPage4kMask    = 0xFFFFF000u;
constexpr uint32_t kPage1kMask    = 0xFFFFFC00u;

constexpr uint32_t kDefinedPteh   = 1u << 0;
constexpr uint32_t kDefinedPtel   = 1u << 1;
constexpr uint32_t kDefinedTtb    = 1u << 2;
constexpr uint32_t kDefinedTea    = 1u << 3;
constexpr uint32_t kMmucrSystem   = kMmucrSv | kMmucrIx;

constexpr uint32_t kExpevtMissRead   = 0x040u;
constexpr uint32_t kExpevtMissWrite  = 0x060u;
constexpr uint32_t kExpevtFirstWrite = 0x080u;
constexpr uint32_t kExpevtProtRead   = 0x0A0u;
constexpr uint32_t kExpevtProtWrite  = 0x0C0u;
constexpr uint32_t kExpevtAddrRead   = 0x0E0u;
constexpr uint32_t kExpevtAddrWrite  = 0x100u;

}

void ShMmu::OnReady() {
    cpu_   = &engine_.Get<ShCpu>();
    state_ = cpu_->State();
    space_ = &engine_.Get<ShTranslationCache>().Space();
    Reset(ShResetKind::kPowerOn);
}

void ShMmu::Reset(ShResetKind kind) {
    if (kind == ShResetKind::kPowerOn) {
        for (auto& set : regs_.tlb) {
            for (ShTlbEntry& e : set) e = ShTlbEntry{};
        }
    }
    regs_.mmucr   = 0u;
    regs_.pteh    = 0u;
    regs_.ptel    = 0u;
    regs_.ttb     = 0u;
    regs_.tea     = 0u;
    regs_.defined = 0u;
    space_->JumpCacheFlush();
}

uint32_t ShMmu::RequireDefined(uint32_t mask, const char* name, uint32_t value) {
    if ((regs_.defined & mask) == 0u) {
        engine_.Halt(HaltReason::kUnimplemented, "ShMmu: %s read at PC 0x%08X before the guest wrote it", name,
                     state_->pc);
    }
    return value;
}

void ShMmu::RequireUntranslatedPc(const char* name, uint32_t value) {
    if (ShAddressSpace::Paged(state_->pc)) {
        engine_.Halt(HaltReason::kUnimplemented, "ShMmu: %s write of 0x%08X at PC 0x%08X outside P1 and P2", name,
                     value, state_->pc);
    }
}

uint32_t ShMmu::ReadMmucr() { return regs_.mmucr; }

void ShMmu::WriteMmucr(uint32_t value) {
    RequireUntranslatedPc("MMUCR", value);
    const uint32_t next = value & kMmucrStored;
    if ((value & kMmucrTf) == 0u && ((next ^ regs_.mmucr) & kMmucrSystem) != 0u) {
        engine_.Halt(HaltReason::kUnimplemented, "ShMmu: MMUCR write of 0x%08X at PC 0x%08X changes SV or IX without TF",
                     value, state_->pc);
    }
    if ((value & kMmucrTf) != 0u) {
        for (auto& set : regs_.tlb) {
            for (ShTlbEntry& e : set) e.valid = false;
        }
        space_->JumpCacheFlush();
    }
    if (((next ^ regs_.mmucr) & kMmucrMapping) != 0u) space_->JumpCacheFlush();
    regs_.mmucr = next;
}

uint32_t ShMmu::ReadPteh() { return RequireDefined(kDefinedPteh, "PTEH", regs_.pteh); }

void ShMmu::WritePteh(uint32_t value) {
    RequireUntranslatedPc("PTEH", value);
    const uint32_t next = value & kPtehWritable;
    if (((next ^ regs_.pteh) & kAsidMask) != 0u || (regs_.defined & kDefinedPteh) == 0u) space_->JumpCacheFlush();
    regs_.pteh = next;
    regs_.defined |= kDefinedPteh;
}

uint32_t ShMmu::ReadPtel() { return RequireDefined(kDefinedPtel, "PTEL", regs_.ptel); }

void ShMmu::WritePtel(uint32_t value) {
    RequireUntranslatedPc("PTEL", value);
    regs_.ptel = value & kPtelWritable;
    regs_.defined |= kDefinedPtel;
}

uint32_t ShMmu::ReadTtb() { return RequireDefined(kDefinedTtb, "TTB", regs_.ttb); }

void ShMmu::WriteTtb(uint32_t value) {
    RequireUntranslatedPc("TTB", value);
    regs_.ttb = value;
    regs_.defined |= kDefinedTtb;
}

uint32_t ShMmu::ReadTea() { return RequireDefined(kDefinedTea, "TEA", regs_.tea); }

void ShMmu::WriteTea(uint32_t value) {
    RequireUntranslatedPc("TEA", value);
    regs_.tea = value;
    regs_.defined |= kDefinedTea;
}

bool ShMmu::AddressTranslation() const { return (regs_.mmucr & kMmucrAt) != 0u; }

bool ShMmu::Translates(uint32_t va) const { return AddressTranslation() && ShAddressSpace::Paged(va); }

uint32_t ShMmu::SetIndex(uint32_t vpn, uint8_t asid) const {
    const uint32_t index = (vpn >> kIndexShift) & kIndexMask;
    return (regs_.mmucr & kMmucrIx) != 0u ? index ^ (asid & kIndexMask) : index;
}

bool ShMmu::Matches(const ShTlbEntry& e, uint32_t va) const {
    if (!e.defined) return false;
    const uint32_t diff = e.vpn ^ va;
    if ((diff & kVpnHigh) != 0u) return false;
    if ((e.data & kDataSz) == 0u && (diff & kVpn1k) != 0u) return false;
    const bool privileged_single = (regs_.mmucr & kMmucrSv) != 0u && (state_->sr & kShSrMd) != 0u;
    if ((e.data & kDataSh) != 0u || privileged_single) return true;
    return e.asid == Asid();
}

ShMmu::Fault ShMmu::Lookup(uint32_t va, ShAccess access, ShTranslation* out, uint32_t* way) {
    const ShTlbEntry* set     = regs_.tlb[SetIndex(va, Asid())];
    uint32_t          hits    = 0;
    bool              invalid = false;
    for (uint32_t w = 0; w < kShTlbWays; ++w) {
        if (!Matches(set[w], va)) continue;
        if (!set[w].valid) {
            if (!invalid && hits == 0u) *way = w;
            invalid = true;
            continue;
        }
        ++hits;
        *way = w;
    }
    if (hits > 1u) return Fault::kMultiHit;
    if (hits == 0u) return invalid ? Fault::kInvalid : Fault::kMiss;
    const ShTlbEntry& e = set[*way];
    const uint32_t pr         = (e.data >> kDataPrShift) & 3u;
    const bool     write      = access == ShAccess::kWrite;
    const bool     privileged = (state_->sr & kShSrMd) != 0u;
    const bool     readable   = privileged || pr >= 2u;
    const bool     writable   = privileged ? (pr & 1u) != 0u : pr == 3u;
    if (!readable || (write && !writable)) return Fault::kProtection;
    if (write && (e.data & kDataD) == 0u) return Fault::kInitialWrite;
    const uint32_t pa = (e.data & kDataSz) != 0u ? (e.data & kPage4kMask) | (va & ~kPage4kMask)
                                                 : (e.data & kPage1kMask) | (va & ~kPage1kMask);
    out->pa     = pa & UnmappedWindows::kPaMask;
    out->global = (e.data & kDataSh) != 0u || ((regs_.mmucr & kMmucrSv) != 0u && privileged);
    return Fault::kNone;
}

void ShMmu::Raise(Fault fault, uint32_t va, ShAccess access, uint32_t way, uint32_t exception_pc) {
    const bool write = access == ShAccess::kWrite;
    if (!cpu_->BlocksExceptions()) {
        uint32_t rc = way;
        if (fault == Fault::kMiss) {
            const ShTlbEntry* set = regs_.tlb[SetIndex(va, Asid())];
            rc                    = ((regs_.mmucr >> kMmucrRcShift) + 1u) & 3u;
            for (uint32_t w = 0; w < kShTlbWays; ++w) {
                if (!set[w].valid) {
                    rc = w;
                    break;
                }
            }
        }
        regs_.mmucr = (regs_.mmucr & ~kMmucrRc) | (rc << kMmucrRcShift);
        LatchFaultAddress(va);
    }
    uint32_t expevt = write ? kExpevtMissWrite : kExpevtMissRead;
    if (fault == Fault::kProtection) expevt = write ? kExpevtProtWrite : kExpevtProtRead;
    if (fault == Fault::kInitialWrite) expevt = kExpevtFirstWrite;
    cpu_->RaiseException(expevt, fault == Fault::kMiss ? kShVectorTlbMiss : kShVectorGeneral, exception_pc);
}

void ShMmu::LatchFaultAddress(uint32_t va) {
    regs_.tea  = va;
    regs_.pteh = (va & kVpnMask) | (regs_.pteh & kAsidMask);
    regs_.defined |= kDefinedTea;
}

void ShMmu::RaiseAddressError(uint32_t va, ShAccess access, uint32_t exception_pc) {
    if (!cpu_->BlocksExceptions()) LatchFaultAddress(va);
    cpu_->RaiseException(access == ShAccess::kWrite ? kExpevtAddrWrite : kExpevtAddrRead, kShVectorGeneral,
                         exception_pc);
}

bool ShMmu::Translate(uint32_t va, ShAccess access, uint32_t exception_pc, ShTranslation* out) {
    RequireDefined(kDefinedPteh, "PTEH", regs_.pteh);
    uint32_t    way   = 0;
    const Fault fault = Lookup(va, access, out, &way);
    if (fault == Fault::kNone) return true;
    if (fault == Fault::kMultiHit) {
        engine_.Halt(HaltReason::kUnimplemented, "ShMmu: VA 0x%08X hits more than one valid TLB way at PC 0x%08X",
                     va, state_->pc);
    }
    Raise(fault, va, access, way, exception_pc);
    return false;
}

bool ShMmu::Probe(uint32_t va, ShAccess access, ShTranslation* out) {
    if ((regs_.defined & kDefinedPteh) == 0u) return false;
    uint32_t way = 0;
    return Lookup(va, access, out, &way) == Fault::kNone;
}

bool ShMmu::ProbeFetch(uint32_t va, ShTranslation* out) {
    if (UnmappedWindows::Contains(va) || !Translates(va)) {
        out->pa     = UnmappedWindows::Pa(va);
        out->global = true;
        return true;
    }
    return Probe(va, ShAccess::kFetch, out);
}

void ShMmu::ClearPage(const ShTlbEntry& e) {
    if (e.defined) space_->JumpCacheClearPage((e.vpn & kVpnHigh) | (regs_.pteh & kVpnIndex));
}

void ShMmu::LoadTlb() {
    RequireDefined(kDefinedPteh, "PTEH", regs_.pteh);
    RequireDefined(kDefinedPtel, "PTEL", regs_.ptel);
    if (Translates(state_->pc)) {
        engine_.Halt(HaltReason::kUnimplemented, "ShMmu: LDTLB at PC 0x%08X in a translated area", state_->pc);
    }
    ShTlbEntry& e = regs_.tlb[SetIndex(regs_.pteh, Asid())][(regs_.mmucr & kMmucrRc) >> kMmucrRcShift];
    ClearPage(e);
    e.vpn     = regs_.pteh & kVpnMask;
    e.asid    = Asid();
    e.data    = regs_.ptel & kDataStored;
    e.valid   = (regs_.ptel & kPtelV) != 0u;
    e.defined = true;
    ClearPage(e);
}

void CERFENGINE_CDECL ShMmu::LoadTlbHelper(ShMmu* mmu) { mmu->LoadTlb(); }

void CERFENGINE_CDECL ShMmu::ReturnAfterLdtlbHelper(ShMmu* mmu) {
    if (mmu->Translates(mmu->state_->spc)) {
        mmu->engine_.Halt(HaltReason::kUnimplemented,
                          "ShMmu: RTE at PC 0x%08X within two instructions after LDTLB returns to VA 0x%08X",
                          mmu->state_->pc, mmu->state_->spc);
    }
}

}
