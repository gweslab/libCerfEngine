#pragma once

#include <cstdint>

inline constexpr uint32_t kStoreMmioPa = 0x10000000u;

inline constexpr uint32_t kArmStoreRamBase = 0x00000000u;
inline constexpr uint32_t kArmStoreRamSize = 0x00010000u;

/* ARM DDI 0406C.c A8.8.102 MOV (immediate) A1, A8.8.5 ADD (immediate, ARM) A1,
   A8.8.204 STR (immediate, ARM) A1, A8.8.18 B A1. */
inline constexpr uint32_t kArmStoreProgram[] = {
    0xE3A0002Au,
    0xE2801001u,
    0xE3A02201u,
    0xE5821000u,
    0xEAFFFFFEu,
};

inline constexpr uint32_t kMipsStoreRamBase = 0x1FC00000u;
inline constexpr uint32_t kMipsStoreRamSize = 0x00010000u;
inline constexpr uint32_t kMipsStoreEntryVa = 0xBFC00000u;

/* MIPS64 Architecture For Programmers Volume II rev 2.50: ADDIU (p.41), LUI (p.217), SW (p.339),
   BEQ (p.64), NOP (p.275). */
inline constexpr uint32_t kMipsStoreProgram[] = {
    0x2402002Au,
    0x24430001u,
    0x3C04B000u,
    0xAC830000u,
    0x1000FFFFu,
    0x00000000u,
};
