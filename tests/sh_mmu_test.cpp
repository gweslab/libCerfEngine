#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using cerfengine::ShAccess;
using cerfengine::ShCpuState;
using cerfengine::ShMmu;
using cerfengine::ShTranslation;

constexpr uint32_t kSrBank0 = cerfengine::kShSrMd | cerfengine::kShSrImask;

constexpr uint32_t kBankVbr       = 0xA0011000u;
constexpr uint32_t kQuickVbr      = 0xA0012000u;
constexpr uint32_t kTlbMissVector = 0x400u;

constexpr uint32_t kExpevtTlbRead      = 0x040u;
constexpr uint32_t kExpevtInitialWrite = 0x080u;
constexpr uint32_t kExpevtProtWrite    = 0x0C0u;

constexpr uint32_t kDataWords = 0x20u;
constexpr uint32_t kDataTag   = 0xC0DE0000u;
constexpr uint32_t kCacheVa   = 0xF0000010u;
constexpr uint32_t kR10Start  = 0x100u;

constexpr uint16_t kStoreR2R1  = 0x2122u;
constexpr uint16_t kStoreR3R4  = 0x2432u;
constexpr uint16_t kLdtlb      = 0x0038u;
constexpr uint16_t kAddOneR8   = 0x7801u;
constexpr uint16_t kAddOneR10  = 0x7A01u;
constexpr uint16_t kAdd16R10   = 0x7A10u;
constexpr uint16_t kStcSrR11   = 0x0B02u;
constexpr uint16_t kMovR0R12   = 0x6C03u;
constexpr uint16_t kMov55R0    = 0xE055u;
constexpr uint32_t kHandlerR0  = 0x55u;

uint32_t DataAt(uint32_t offset) { return kDataTag | offset; }

ShCpuState Initial(uint32_t sr, uint32_t r1) {
    ShCpuState s{};
    s.sr      = sr;
    s.gpr[1]  = r1;
    s.gpr[10] = kR10Start;
    return s;
}

struct Exception {
    uint32_t expevt;
    uint32_t va;
    uint32_t spc;
    uint32_t rc;
    uint32_t sr;
    uint8_t  asid;
};

void ExpectException(ShIrRig& rig, const char* mode, const char* what, const Exception& want) {
    const ShCpuState& s   = rig.State();
    ShMmu&            mmu = rig.Mmu();
    ShRigExpect(mode, what, "EXPEVT", s.expevt, want.expevt);
    ShRigExpect(mode, what, "TEA", mmu.ReadTea(), want.va);
    ShRigExpect(mode, what, "PTEH", mmu.ReadPteh(), (want.va & kShRigVpnMask) | want.asid);
    ShRigExpect(mode, what, "SPC", s.spc, want.spc);
    ShRigExpect(mode, what, "SSR", s.ssr, want.sr);
    ShRigExpect(mode, what, "MMUCR.RC", (mmu.ReadMmucr() & kMmucrRc) >> kMmucrRcShift, want.rc);
    ShRigExpect(mode, what, "SR", s.sr, want.sr);
    ShRigExpect(mode, what, "R11 (SR in the RTE slot)", s.gpr[11], want.sr);
}

void ExpectHandlers(ShIrRig& rig, const char* mode, const char* what, uint32_t misses, uint32_t general) {
    ShRigExpect(mode, what, "TLB miss handler entries", rig.State().gpr[8], misses);
    ShRigExpect(mode, what, "general handler entries", rig.State().gpr[9], general);
}

void LoadMiss(ShIrRig& rig, const char* mode) {
    const char*    what = "load TLB miss";
    const uint32_t va   = 0x00400010u;
    ShMmu&         mmu  = rig.Mmu();
    const uint32_t start = rig.Run({kShRigLoadLR1R2}, Initial(kShRigSrPrivileged, va), what, [&](ShCpuState& s) {
        s.vbr = kShRigVbr;
        rig.ResetMmu(0u, kShRigAsid);
        mmu.WritePtel(kShRigDataPa | kPage4kRw);
    });
    ShRigExpect(mode, what, "R2", rig.State().gpr[2], DataAt(va & 0xFFFu));
    ExpectHandlers(rig, mode, what, 1u, 0u);
    ExpectException(rig, mode, what, {kExpevtTlbRead, va, start, 0u, kShRigSrPrivileged, kShRigAsid});
    ShTranslation t{};
    if (!mmu.Probe(va, ShAccess::kRead, &t) || t.pa != kShRigDataPa + (va & 0xFFFu)) {
        TestFail("%s %s: the loaded entry does not translate 0x%08X", mode, what, va);
    }
}

void SlotLoadMiss(ShIrRig& rig, const char* mode) {
    const char*    what = "TLB miss in a delay slot";
    const uint32_t va   = 0x00400020u;
    ShMmu&         mmu  = rig.Mmu();
    const uint32_t start =
        rig.Run({kShRigBraNextNop, kShRigLoadLR1R2}, Initial(kShRigSrPrivileged, va), what, [&](ShCpuState& s) {
            s.vbr = kShRigVbr;
            rig.ResetMmu(0u, kShRigAsid);
            mmu.WritePtel(kShRigDataPa | kPage4kRw);
        });
    ShRigExpect(mode, what, "R2", rig.State().gpr[2], DataAt(va & 0xFFFu));
    ExpectHandlers(rig, mode, what, 1u, 0u);
    ExpectException(rig, mode, what, {kExpevtTlbRead, va, start, 0u, kShRigSrPrivileged, kShRigAsid});
}

void InitialPageWrite(ShIrRig& rig, const char* mode) {
    const char*    what  = "initial page write";
    const uint32_t va    = 0x00800000u;
    const uint32_t clean = kShRigMmioPa | kPtelV | kPtelPrRw | kPtelSz;
    const uint32_t value = 0x5EED0003u;
    ShMmu&         mmu   = rig.Mmu();
    ShCpuState     init  = Initial(kShRigSrPrivileged, va);
    init.gpr[2]          = value;
    const uint32_t start = rig.Run({kStoreR2R1}, init, what, [&](ShCpuState& s) {
        s.vbr = kShRigVbr;
        rig.ResetMmu(0u, kShRigAsid);
        rig.Preload(2u, va, kShRigAsid, clean);
        mmu.WritePtel(clean | kPtelD);
    });
    ShRigExpect(mode, what, "MMIO value", static_cast<uint32_t>(rig.Host().MmioValue()), value);
    ExpectHandlers(rig, mode, what, 0u, 1u);
    ExpectException(rig, mode, what, {kExpevtInitialWrite, va, start, 2u, kShRigSrPrivileged, kShRigAsid});
}

void ProtectionWrite(ShIrRig& rig, const char* mode) {
    const char*    what      = "protection violation on a write";
    const uint32_t va        = 0x00801000u;
    const uint32_t read_only = kShRigMmioPa | kPtelV | kPtelSz | kPtelD;
    const uint32_t value     = 0x5EED0004u;
    ShMmu&         mmu       = rig.Mmu();
    ShCpuState     init      = Initial(kShRigSrPrivileged, va);
    init.gpr[2]              = value;
    const uint32_t start     = rig.Run({kStoreR2R1}, init, what, [&](ShCpuState& s) {
        s.vbr = kShRigVbr;
        rig.ResetMmu(0u, kShRigAsid);
        rig.Preload(1u, va, kShRigAsid, read_only);
        mmu.WritePtel(read_only | kPtelPrRw);
    });
    ShRigExpect(mode, what, "MMIO value", static_cast<uint32_t>(rig.Host().MmioValue()), value);
    ExpectHandlers(rig, mode, what, 0u, 1u);
    ExpectException(rig, mode, what, {kExpevtProtWrite, va, start, 1u, kShRigSrPrivileged, kShRigAsid});
}

void InvalidEntry(ShIrRig& rig, const char* mode) {
    const char*    what    = "TLB invalid";
    const uint32_t va      = 0x00402030u;
    const uint32_t invalid = kShRigDataPa | kPtelPrRw | kPtelSz | kPtelD;
    ShMmu&         mmu     = rig.Mmu();
    const uint32_t start   = rig.Run({kShRigLoadLR1R2}, Initial(kShRigSrPrivileged, va), what, [&](ShCpuState& s) {
        s.vbr = kShRigVbr;
        rig.ResetMmu(0u, kShRigAsid);
        rig.Preload(3u, va & kShRigVpnMask, kShRigAsid, invalid);
        mmu.WritePtel(invalid | kPtelV);
    });
    ShRigExpect(mode, what, "R2", rig.State().gpr[2], DataAt(va & 0xFFFu));
    ExpectHandlers(rig, mode, what, 0u, 1u);
    ExpectException(rig, mode, what, {kExpevtTlbRead, va, start, 3u, kShRigSrPrivileged, kShRigAsid});
}

void FetchMiss(ShIrRig& rig, const char* mode) {
    const char*    what    = "instruction fetch TLB miss";
    const uint32_t code_va = 0x00600000u;
    const uint32_t code_pa = 0x00019000u;
    ShMmu&         mmu     = rig.Mmu();
    rig.LoadCode(code_pa, {kAddOneR10, kShRigRts, kShRigNop});
    rig.Run({kShRigJsrR1, kShRigNop}, Initial(kShRigSrPrivileged, code_va), what, [&](ShCpuState& s) {
        s.vbr = kShRigVbr;
        rig.ResetMmu(0u, kShRigAsid);
        mmu.WritePtel(code_pa | kPage4kRw);
    });
    ShRigExpect(mode, what, "R10", rig.State().gpr[10], kR10Start + 1u);
    ExpectHandlers(rig, mode, what, 1u, 0u);
    ExpectException(rig, mode, what, {kExpevtTlbRead, code_va, code_va, 0u, kShRigSrPrivileged, kShRigAsid});
}

void SlotFetchMiss(ShIrRig& rig, const char* mode) {
    const char*    what      = "delay slot fetch on an unmapped next page";
    const uint32_t page_va   = 0x00700000u;
    const uint32_t page_pa   = 0x0001A000u;
    const uint32_t next_va   = page_va + 0x400u;
    const uint32_t next_pa   = page_pa + 0x400u;
    const uint32_t branch_va = next_va - 2u;
    ShMmu&         mmu       = rig.Mmu();
    rig.LoadCode(next_pa - 4u, {kAddOneR10, kShRigRts});
    rig.LoadCode(next_pa, {kShRigNop, kShRigNop});
    rig.Run({kShRigJsrR1, kShRigNop}, Initial(kShRigSrPrivileged, next_va - 4u), what, [&](ShCpuState& s) {
        s.vbr = kShRigVbr;
        rig.ResetMmu(0u, kShRigAsid);
        rig.Preload(0u, page_va, kShRigAsid, page_pa | kPage1kRw);
        mmu.WritePtel(next_pa | kPage1kRw);
    });
    ShRigExpect(mode, what, "R10", rig.State().gpr[10], kR10Start + 1u);
    ExpectHandlers(rig, mode, what, 1u, 0u);
    ExpectException(rig, mode, what, {kExpevtTlbRead, next_va, branch_va, 1u, kShRigSrPrivileged, kShRigAsid});
}

void RteRestoresBankBeforeSlot(ShIrRig& rig, const char* mode) {
    const char*    what   = "RTE slot under the restored register bank";
    const uint32_t va     = 0x00403040u;
    const uint32_t body_r0 = 0x0000ABCDu;
    ShMmu&         mmu    = rig.Mmu();
    ShCpuState     init   = Initial(kSrBank0, va);
    init.gpr[0]           = body_r0;
    rig.Run({kShRigLoadLR1R2}, init, what, [&](ShCpuState& s) {
        s.vbr = kBankVbr;
        rig.ResetMmu(0u, kShRigAsid);
        mmu.WritePtel(kShRigDataPa | kPage4kRw);
    });
    const ShCpuState& s = rig.State();
    ShRigExpect(mode, what, "R2", s.gpr[2], DataAt(va & 0xFFFu));
    ShRigExpect(mode, what, "R12 (R0 in the RTE slot)", s.gpr[12], body_r0);
    ShRigExpect(mode, what, "R0", s.gpr[0], body_r0);
    ShRigExpect(mode, what, "R0_BANK1", s.bank[0], kHandlerR0);
    ShRigExpect(mode, what, "SR", s.sr, kSrBank0);
    ShRigExpect(mode, what, "SSR", s.ssr, kSrBank0);
    ExpectHandlers(rig, mode, what, 1u, 0u);
}

void RteRightAfterLdtlb(ShIrRig& rig, const char* mode) {
    const char*    what = "RTE right after LDTLB returning to P2";
    const uint32_t va   = 0x00404050u;
    ShMmu&         mmu  = rig.Mmu();
    const uint32_t start = rig.Run({kShRigLoadLR1R2}, Initial(kShRigSrPrivileged, va), what, [&](ShCpuState& s) {
        s.vbr = kQuickVbr;
        rig.ResetMmu(0u, kShRigAsid);
        mmu.WritePtel(kShRigDataPa | kPage4kRw);
    });
    ShRigExpect(mode, what, "R2", rig.State().gpr[2], DataAt(va & 0xFFFu));
    ExpectHandlers(rig, mode, what, 1u, 0u);
    ExpectException(rig, mode, what, {kExpevtTlbRead, va, start, 0u, kShRigSrPrivileged, kShRigAsid});
}

void UntranslatedLoadAfterLdtlb(ShIrRig& rig, const char* mode) {
    const char*    what = "P2 load within two instructions after LDTLB";
    const uint32_t va   = kShRigVaBase + kShRigDataPa + 0x54u;
    ShMmu&         mmu  = rig.Mmu();
    rig.Run({kLdtlb, kShRigLoadLR1R2, kShRigNop}, Initial(kShRigSrPrivileged, va), what, [&](ShCpuState& s) {
        s.vbr = kShRigVbr;
        rig.ResetMmu(0u, kShRigAsid);
        mmu.WritePteh(0x00405000u | kShRigAsid);
        mmu.WritePtel(kShRigDataPa | kPage4kRw);
    });
    ShRigExpect(mode, what, "R2", rig.State().gpr[2], DataAt(0x54u));
    ExpectHandlers(rig, mode, what, 0u, 0u);
    ShTranslation t{};
    if (!mmu.Probe(0x00405054u, ShAccess::kRead, &t) || t.pa != kShRigDataPa + 0x54u) {
        TestFail("%s %s: the LDTLB entry does not translate", mode, what);
    }
}

void RemapSplitTail(ShIrRig& rig, const char* mode) {
    const char*    what     = "LDTLB remaps the delay slot page of a split block";
    const uint32_t page_va  = 0x00710000u;
    const uint32_t page_pa  = 0x0001F000u;
    const uint32_t tail_va  = page_va + 0x400u;
    const uint32_t old_tail = 0x0001F400u;
    const uint32_t new_tail = 0x0001F800u;
    ShMmu&         mmu      = rig.Mmu();
    rig.LoadCode(page_pa + 0x3FCu, {kShRigNop, kShRigRts});
    rig.LoadCode(old_tail, {kAddOneR10, kShRigNop});
    rig.LoadCode(new_tail, {kAdd16R10, kShRigNop});
    rig.Run({kShRigJsrR1, kShRigNop, kLdtlb, kShRigNop, kShRigNop, kShRigJsrR1, kShRigNop},
            Initial(kShRigSrPrivileged, tail_va - 2u), what, [&](ShCpuState& s) {
                s.vbr = kShRigVbr;
                rig.ResetMmu(0u, kShRigAsid);
                rig.Preload(0u, page_va, kShRigAsid, page_pa | kPage1kRw);
                rig.Preload(1u, tail_va, kShRigAsid, old_tail | kPage1kRw);
                mmu.WritePtel(new_tail | kPage1kRw);
            });
    ShRigExpect(mode, what, "R10", rig.State().gpr[10], kR10Start + 1u + 16u);
    ExpectHandlers(rig, mode, what, 0u, 0u);
}

void CacheArrayInvalidate(ShIrRig& rig, const char* mode) {
    const char* what = "cache address array invalidation";
    ShCpuState  init = Initial(kShRigSrPrivileged, kCacheVa);
    init.gpr[2]      = 0x12345670u;
    init.gpr[3]      = 0u;
    init.gpr[4]      = kCacheVa + 0x18u;
    rig.Run({kStoreR2R1, kStoreR3R4}, init, what, [&](ShCpuState& s) { s.vbr = kShRigVbr; });
    ExpectHandlers(rig, mode, what, 0u, 0u);
}

struct Remap {
    const char* what;
    uint32_t    mmucr_mode;
    uint32_t    old_va;
    uint8_t     old_asid;
    uint32_t    va;
    uint32_t    load_va;
    uint8_t     asid;
    uint32_t    old_pa;
    uint32_t    pa;
    uint32_t    shared;
    uint32_t    misses;
};

void ReplaceCodePage(ShIrRig& rig, const char* mode, const Remap& r) {
    ShMmu& mmu = rig.Mmu();
    rig.LoadCode(r.old_pa, {kAddOneR10, kShRigRts, kShRigNop});
    rig.LoadCode(r.pa, {kAdd16R10, kShRigRts, kShRigNop});
    rig.Run({kShRigJsrR1, kShRigNop, kLdtlb, kShRigNop, kShRigJsrR1, kShRigNop},
            Initial(kShRigSrPrivileged, r.va), r.what, [&](ShCpuState& s) {
                s.vbr = kShRigVbr;
                rig.ResetMmu(r.mmucr_mode, r.old_asid);
                rig.Preload(0u, r.old_va, r.old_asid, r.old_pa | kPage4kRw | r.shared);
                mmu.WritePteh(r.load_va | r.asid);
                mmu.WritePtel(r.pa | kPage4kRw | r.shared);
                ShTranslation t{};
                if (!mmu.Probe(r.va, ShAccess::kFetch, &t) || t.pa != r.old_pa) {
                    TestFail("%s %s: 0x%08X does not reach the old entry", mode, r.what, r.va);
                }
            });
    ShRigExpect(mode, r.what, "R10", rig.State().gpr[10], kR10Start + 1u + 16u);
    ExpectHandlers(rig, mode, r.what, r.misses, 0u);
    if (r.misses != 0u) {
        ExpectException(rig, mode, r.what, {kExpevtTlbRead, r.va, r.va, 1u, kShRigSrPrivileged, r.asid});
    }
}

void ExpectProbe(ShIrRig& rig, const char* what, uint32_t va, bool hit, uint32_t pa, bool global) {
    ShTranslation t{};
    const bool    got = rig.Mmu().Probe(va, ShAccess::kRead, &t);
    if (got != hit) TestFail("match rules %s: Probe(0x%08X) = %d, want %d", what, va, got ? 1 : 0, hit ? 1 : 0);
    if (hit && (t.pa != pa || t.global != global)) {
        TestFail("match rules %s: 0x%08X -> PA 0x%08X global %d, want PA 0x%08X global %d", what, va, t.pa,
                 t.global ? 1 : 0, pa, global ? 1 : 0);
    }
}

void MatchRules(ShIrRig& rig) {
    ShMmu& mmu = rig.Mmu();
    rig.Run({kShRigNop}, Initial(kShRigSrPrivileged, 0u), "match rules", [&](ShCpuState& s) { s.vbr = kShRigVbr; });

    if (mmu.ReadMmucr() != 0u) TestFail("match rules: MMUCR reads 0x%08X after a power-on reset", mmu.ReadMmucr());
    mmu.WriteMmucr(kMmucr620lx);
    if (mmu.ReadMmucr() != kMmucrAt) TestFail("match rules: MMUCR reads 0x%08X after 0x0D", mmu.ReadMmucr());

    rig.ResetMmu(0u, 5u);
    rig.Preload(0u, 0x00440000u, 5u, kShRigDataPa | kPage4kRw);
    ExpectProbe(rig, "same ASID", 0x00440004u, true, kShRigDataPa + 4u, false);
    mmu.WritePteh(6u);
    ExpectProbe(rig, "other ASID", 0x00440004u, false, 0u, false);
    rig.ResetMmu(kMmucrSv, 5u);
    rig.Preload(0u, 0x00440000u, 5u, kShRigDataPa | kPage4kRw);
    mmu.WritePteh(6u);
    ExpectProbe(rig, "single virtual memory in privileged mode", 0x00440004u, true, kShRigDataPa + 4u, true);
    rig.ResetMmu(0u, 5u);
    rig.Preload(1u, 0x00460000u, 5u, kShRigDataPa | kPage4kRw | kPtelSh);
    mmu.WritePteh(6u);
    ExpectProbe(rig, "shared page", 0x00460008u, true, kShRigDataPa + 8u, true);

    rig.ResetMmu(0u, kShRigAsid);
    rig.Preload(0u, 0x00480400u, kShRigAsid, (kShRigDataPa + 0x400u) | kPage1kRw);
    ExpectProbe(rig, "1 KB page", 0x00480410u, true, kShRigDataPa + 0x410u, false);
    ExpectProbe(rig, "1 KB page, other VPN bits 11-10", 0x00480010u, false, 0u, false);
    rig.Preload(1u, 0x004A0000u, kShRigAsid, kShRigDataPa | kPage4kRw);
    ExpectProbe(rig, "4 KB page", 0x004A0C10u, true, kShRigDataPa + 0xC10u, false);
    rig.Preload(2u, 0x004C0000u, kShRigAsid, (0x60000000u | kShRigDataPa) | kPage4kRw);
    ExpectProbe(rig, "PPN above the 29-bit space", 0x004C0020u, true, kShRigDataPa + 0x20u, false);
    mmu.WriteMmucr(kMmucrAt | kMmucrTf);
    ExpectProbe(rig, "after TLB flush", 0x004A0C10u, false, 0u, false);

    rig.ResetMmu(kMmucrIx, 3u);
    rig.Preload(0u, 0x00510000u, 3u, kShRigDataPa | kPage4kRw);
    ExpectProbe(rig, "IX index, same ASID", 0x00510000u, true, kShRigDataPa, false);
    mmu.WritePteh(5u);
    ExpectProbe(rig, "IX index, unshared alias", 0x00516000u, false, 0u, false);
    ExpectProbe(rig, "IX index, other set", 0x00510000u, false, 0u, false);
}

void RunAll(bool verify, const char* mode) {
    ShIrRig rig(false, verify);
    rig.LoadCode(ShRigPa(kShRigVbr + kTlbMissVector), {kAddOneR8, kLdtlb, kShRigNop, kShRigNop, kShRigRte, kStcSrR11});
    rig.LoadCode(ShRigPa(kShRigVbr + kShRigGeneralVector),
                 {kShRigAddOneR9, kLdtlb, kShRigNop, kShRigNop, kShRigRte, kStcSrR11});
    rig.LoadCode(ShRigPa(kBankVbr + kTlbMissVector),
                 {kMov55R0, kAddOneR8, kLdtlb, kShRigNop, kShRigNop, kShRigRte, kMovR0R12});
    rig.LoadCode(ShRigPa(kQuickVbr + kTlbMissVector), {kAddOneR8, kLdtlb, kShRigRte, kStcSrR11});
    std::vector<uint32_t> data(kDataWords);
    for (uint32_t i = 0; i < kDataWords; ++i) data[i] = DataAt(i * 4u);
    rig.Host().LoadWords(kShRigDataPa, data.data(), data.size());

    LoadMiss(rig, mode);
    SlotLoadMiss(rig, mode);
    InitialPageWrite(rig, mode);
    ProtectionWrite(rig, mode);
    InvalidEntry(rig, mode);
    FetchMiss(rig, mode);
    SlotFetchMiss(rig, mode);
    RteRestoresBankBeforeSlot(rig, mode);
    RteRightAfterLdtlb(rig, mode);
    UntranslatedLoadAfterLdtlb(rig, mode);
    RemapSplitTail(rig, mode);
    CacheArrayInvalidate(rig, mode);
    ReplaceCodePage(rig, mode,
                    {"LDTLB replaces a mapped code page", 0u, 0x00500000u, kShRigAsid, 0x00500000u, 0x00500000u,
                     kShRigAsid, 0x0001B000u, 0x0001C000u, 0u, 0u});
    ReplaceCodePage(rig, mode,
                    {"LDTLB evicts a shared code page reached through an IX alias", kMmucrIx, 0x00510000u, 3u,
                     0x00516000u, 0x00536000u, 5u, 0x0001D000u, 0x0001E000u, kPtelSh, 1u});
    MatchRules(rig);
    std::printf("%s: %u blocks compiled\n", mode, rig.BlocksCompiled());
}

}

int main() {
    RunAll(false, "plain");
    RunAll(true, "verified IR");
    std::printf("sh_mmu_test passed\n");
    return 0;
}
