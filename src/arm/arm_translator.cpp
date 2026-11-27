#include <cerfengine/arm/arm_translator.h>

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/physical_map.h>
#include "arm_idle_skip.h"
#include "arm_tlb_ops.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmTranslator);

void ArmTranslator::OnReady() {
    mmu_       = &engine_.Get<ArmMmu>();
    state_     = mmu_->State();
    cpu_state_ = engine_.Get<ArmCpu>().State();
    map_       = &engine_.Get<PhysicalMap>();
    memory_    = engine_.GetSetup().memory;
    walker_    = engine_.GetSetup().arm_translation;
    idle_skip_ = &engine_.Get<ArmIdleSkip>();
    mmu_->BindTranslator(this);
}

template <ArmMmuAccess kAccess, bool kSpan>
void ArmTranslator::NoteCodeAccess(ArmMmuState& st, uint32_t pa,
                                   [[maybe_unused]] uint32_t span_bytes) {
    if constexpr (kSpan) {
        ArmNoteCodeWriteSpan(st, pa, span_bytes);
    } else {
        ArmNoteCodeTracking<kAccess>(st, pa);
    }
}

template <bool kSpan>
void ArmTranslator::NoteStore(uint8_t* host, uint32_t va, [[maybe_unused]] uint32_t span_bytes) {
    if constexpr (kSpan) {
        idle_skip_->NoteStoreSpan(host, va, span_bytes);
    } else {
        idle_skip_->NoteStore(host, va);
    }
}

void ArmTranslator::FillSubpage(ArmTlbUnit* unit, uint32_t p, uint32_t pa, uint8_t asid,
                                const ArmTranslation& walk) {
    uint8_t* const read = map_->Read(pa);
    if (read == nullptr || !host::PhysicalSpanUniform(memory_, pa, walk.span_bytes)) return;
    uint32_t access = walk.subpage_access;
    if (map_->Write(pa) != read || idle_skip_->Recording()) access &= 0x0Fu;
    FillFastTlbSubpage(unit, p, read, pa, asid, walk.global, access);
}

template <ArmMmuAccess kAccess, bool kForceUser, bool kSpan>
uint8_t* ArmTranslator::MapGuestVirtualToHost(uint32_t p, [[maybe_unused]] uint32_t span_bytes) {
    static_assert(!kSpan || kAccess == ArmMmuAccess::kWrite);
    ArmMmuState& st = *state_;
    mmu_->ClearIoPending();

    /* ARM DDI 0406C.c B3.19.3 TranslateAddressV (p. B3-1504): "mva =
       FCSETranslate(va)" precedes the SCTLR.M test. */
    p = ArmFcseFold(p, st.fcse_fold_id);

    if (!st.effective_control_register.bits.m) {
        constexpr bool kStores = kAccess == ArmMmuAccess::kWrite ||
                                 kAccess == ArmMmuAccess::kReadWrite;
        uint8_t* host = kStores ? map_->Write(p) : map_->Read(p);
        if (host) {
            if constexpr (kAccess == ArmMmuAccess::kExecute) {
                last_exec_pa_      = p;
                last_exec_tracked_ = false;
                last_exec_global_  = false;
            }
            return host;
        }
        mmu_->SetIoPending(p);
        return nullptr;
    }

    const bool is_user_mode =
        kForceUser || (cpu_state_->cpsr.bits.mode == ArmMode::kUser);
    ArmTlbUnit* tlb_unit = &((kAccess == ArmMmuAccess::kExecute)
        ? st.instruction_tlb : st.data_tlb)[ArmTlbPrivilege(is_user_mode)];
    const uint8_t current_asid = static_cast<uint8_t>(st.contextidr & 0xFFu);

    const uint32_t va_page  = p & 0xFFFFF000u;
    const uint32_t set_base = ArmTlbSetBase(p);
    if constexpr (!kForceUser) {
        constexpr bool kIsWrite = (kAccess == ArmMmuAccess::kWrite ||
                                   kAccess == ArmMmuAccess::kReadWrite);
        const int hit_way =
            ArmTlbMatchWay(tlb_unit, set_base, va_page, current_asid, kIsWrite);
        if (hit_way >= 0) {
            ArmTlbPromote(tlb_unit, set_base, hit_way);
            const ArmTlbEntry& fast = tlb_unit->entries[set_base];
            const uint32_t pa = fast.pa_page | (p & 0x0FFFu);
            if constexpr (kAccess == ArmMmuAccess::kExecute) {
                last_exec_pa_      = pa;
                last_exec_tracked_ = true;
                last_exec_global_  = fast.global != 0u;
            }
            NoteCodeAccess<kAccess, kSpan>(st, pa, span_bytes);
            uint8_t* const host = reinterpret_cast<uint8_t*>(
                static_cast<uintptr_t>(p) + fast.va_addend);
            if constexpr (kIsWrite) {
                if (idle_skip_->Recording()) NoteStore<kSpan>(host, p, span_bytes);
            }
            return host;
        }

        if constexpr (kAccess != ArmMmuAccess::kExecute) {
            const int io_way = ArmTlbMatchIoWay(tlb_unit, set_base, va_page,
                                                current_asid, kIsWrite);
            if (io_way >= 0) {
                const ArmTlbEntry& io =
                    tlb_unit->entries[set_base + static_cast<uint32_t>(io_way)];
                mmu_->SetIoPending(io.pa_page | (p & 0x0FFFu));
                return nullptr;
            }
            const int sub_way = ArmTlbMatchSubpageWay(tlb_unit, set_base, va_page, current_asid);
            if (sub_way >= 0) {
                const uint32_t needed =
                    (kIsWrite ? 0x10u : 0x01u) << ((p >> kArmTlbSubpageIndexShift) & 3u);
                if ((ArmTlbSubpageAccess(tlb_unit->entries[set_base + static_cast<uint32_t>(sub_way)]) &
                     needed) != 0u) {
                    ArmTlbPromote(tlb_unit, set_base, sub_way);
                    const ArmTlbEntry& sub = tlb_unit->entries[set_base];
                    NoteCodeAccess<kAccess, kSpan>(st, sub.pa_page | (p & 0x0FFFu), span_bytes);
                    uint8_t* const host = reinterpret_cast<uint8_t*>(
                        static_cast<uintptr_t>(p) + sub.va_addend);
                    if constexpr (kIsWrite) {
                        if (idle_skip_->Recording()) NoteStore<kSpan>(host, p, span_bytes);
                    }
                    return host;
                }
            }
        }
    }

    const ArmTranslation walk = host::ArmWalk<kAccess>(walker_, p, is_user_mode,
                                                       st.effective_control_register.word);
    if (!walk.mapped) return nullptr;
    const uint32_t effective_address = walk.pa;
    if (walk.host != nullptr) {
        if constexpr (kAccess == ArmMmuAccess::kExecute) {
            last_exec_pa_      = effective_address;
            last_exec_tracked_ = false;
            last_exec_global_  = false;
        }
        if constexpr (kAccess == ArmMmuAccess::kWrite || kAccess == ArmMmuAccess::kReadWrite) {
            if (idle_skip_->Recording()) NoteStore<kSpan>(walk.host, p, span_bytes);
        }
        return walk.host;
    }

    const bool uniform = walk.cacheable &&
                         host::PhysicalSpanUniform(memory_, effective_address, walk.span_bytes);

    if constexpr (kAccess == ArmMmuAccess::kExecute) {
        last_exec_pa_      = effective_address;
        last_exec_tracked_ = true;
        last_exec_global_  = uniform && walk.global;
    }

    if constexpr (kAccess == ArmMmuAccess::kWrite) {
        uint8_t* host_ptr = map_->Write(effective_address);
        if (host_ptr) {
            const bool recording = idle_skip_->Recording();
            if (uniform) {
                FillFastTlb(tlb_unit, p, host_ptr, effective_address,
                            current_asid, walk.global,
                            !recording, walk.span_bytes);
            } else if (walk.subpage_access != 0u) {
                FillSubpage(tlb_unit, p, effective_address, current_asid, walk);
            }
            NoteCodeAccess<kAccess, kSpan>(st, effective_address, span_bytes);
            if (recording) NoteStore<kSpan>(host_ptr, p, span_bytes);
            return host_ptr;
        }
        if (walk.cacheable && !map_->Read(effective_address)) {
            FillFastTlbIo(tlb_unit, p, effective_address, current_asid,
                          walk.global, true, walk.span_bytes);
        }
        mmu_->SetIoPending(effective_address);
        return nullptr;
    } else {
        constexpr bool kReadWrite = kAccess == ArmMmuAccess::kReadWrite;
        const bool     recording  = kReadWrite && idle_skip_->Recording();
        uint8_t* ram_host = kReadWrite ? map_->Write(effective_address) : map_->Read(effective_address);
        if (ram_host) {
            if (uniform) {
                FillFastTlb(tlb_unit, p, ram_host, effective_address,
                            current_asid, walk.global, kReadWrite && !recording,
                            walk.span_bytes);
            } else if constexpr (kAccess != ArmMmuAccess::kExecute) {
                if (walk.subpage_access != 0u) {
                    FillSubpage(tlb_unit, p, effective_address, current_asid, walk);
                }
            }
            ArmNoteCodeTracking<kAccess>(st, effective_address);
            if (recording) idle_skip_->NoteStore(ram_host, p);
            return ram_host;
        }
        uint8_t* flash_host = map_->Read(effective_address);
        if (flash_host) {
            if (uniform) {
                FillFastTlb(tlb_unit, p, flash_host, effective_address,
                            current_asid, walk.global,
                            false, walk.span_bytes);
            }
            if constexpr (kReadWrite) {
                mmu_->SetIoPending(effective_address);
                return nullptr;
            } else {
                ArmNoteCodeTracking<kAccess>(st, effective_address);
                return flash_host;
            }
        }
        if constexpr (kAccess != ArmMmuAccess::kExecute) {
            if (walk.cacheable) {
                FillFastTlbIo(tlb_unit, p, effective_address, current_asid,
                              walk.global,
                              kAccess == ArmMmuAccess::kReadWrite,
                              walk.span_bytes);
            }
        }
        mmu_->SetIoPending(effective_address);
        return nullptr;
    }
}

uint8_t* ArmTranslator::TranslateRead(uint32_t va) {
    return MapGuestVirtualToHost<ArmMmuAccess::kRead>(va);
}

uint8_t* ArmTranslator::TranslateWrite(uint32_t va) {
    return MapGuestVirtualToHost<ArmMmuAccess::kWrite>(va);
}

uint8_t* ArmTranslator::TranslateReadWrite(uint32_t va) {
    return MapGuestVirtualToHost<ArmMmuAccess::kReadWrite>(va);
}

uint8_t* ArmTranslator::TranslateExecute(uint32_t va) {
    return MapGuestVirtualToHost<ArmMmuAccess::kExecute>(va);
}

uint8_t* ArmTranslator::TranslateUserRead(uint32_t va) {
    return MapGuestVirtualToHost<ArmMmuAccess::kRead, true>(va);
}

uint8_t* ArmTranslator::TranslateUserWrite(uint32_t va) {
    return MapGuestVirtualToHost<ArmMmuAccess::kWrite, true>(va);
}

uint8_t* ArmTranslator::TranslateWriteSpan(uint32_t va, uint32_t bytes) {
    return MapGuestVirtualToHost<ArmMmuAccess::kWrite, false, true>(va, bytes);
}

uint8_t* ArmTranslator::TranslateUserWriteSpan(uint32_t va, uint32_t bytes) {
    return MapGuestVirtualToHost<ArmMmuAccess::kWrite, true, true>(va, bytes);
}

const ArmTlbEntry* ArmTranslator::MatchDataTlb(uint32_t va, uint32_t* folded) const {
    const ArmMmuState& st = *state_;
    const uint32_t p = ArmFcseFold(va, st.fcse_fold_id);
    *folded = p;
    const uint8_t current_asid = static_cast<uint8_t>(st.contextidr & 0xFFu);
    const uint32_t base = ArmTlbSetBase(p);
    const ArmTlbUnit& unit =
        st.data_tlb[ArmTlbPrivilege(cpu_state_->cpsr.bits.mode == ArmMode::kUser)];
    const int w = ArmTlbMatchWay(&unit, base, p & 0xFFFFF000u, current_asid, false);
    if (w < 0) return nullptr;
    return &unit.entries[base + static_cast<uint32_t>(w)];
}

std::optional<uint8_t*> ArmTranslator::PeekDataTlb(uint32_t va) const {
    uint32_t p = 0;
    const ArmTlbEntry* e = MatchDataTlb(va, &p);
    if (!e) return std::nullopt;
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(p) + e->va_addend);
}

uint8_t* ArmTranslator::PeekVaToHost(uint32_t va) {
    const ArmMmuState& st = *state_;
    if (!st.effective_control_register.bits.m) {
        const uint32_t pa = ArmFcseFold(va, st.fcse_fold_id);
        uint8_t* ram = map_->PeekWrite(pa);
        return ram ? ram : map_->PeekRead(pa);
    }

    if (std::optional<uint8_t*> tlb = PeekDataTlb(va)) return *tlb;

    uint32_t pa = 0;
    if (!host::ArmProbeWalk(walker_, ArmFcseFold(va, st.fcse_fold_id),
                            st.effective_control_register.word, &pa)) {
        return nullptr;
    }
    uint8_t* ram = map_->PeekWrite(pa);
    return ram ? ram : map_->PeekRead(pa);
}

bool ArmTranslator::PeekVaToPa(uint32_t va, uint32_t* pa) {
    const ArmMmuState& st = *state_;
    if (!st.effective_control_register.bits.m) {
        *pa = ArmFcseFold(va, st.fcse_fold_id);
        return true;
    }

    uint32_t p = 0;
    if (const ArmTlbEntry* e = MatchDataTlb(va, &p)) {
        *pa = e->pa_page | (p & 0x0FFFu);
        return true;
    }

    return host::ArmProbeWalk(walker_, ArmFcseFold(va, st.fcse_fold_id),
                              st.effective_control_register.word, pa);
}

}
