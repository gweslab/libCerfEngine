#pragma once

#if defined(_MSC_VER)
#define CERFENGINE_CDECL    __cdecl
#define CERFENGINE_FASTCALL __fastcall
#define CERFENGINE_STDCALL  __stdcall
#elif defined(__i386__)
#define CERFENGINE_CDECL    __attribute__((cdecl))
#define CERFENGINE_FASTCALL __attribute__((fastcall))
#define CERFENGINE_STDCALL  __attribute__((stdcall))
#else
#define CERFENGINE_CDECL
#define CERFENGINE_FASTCALL
#define CERFENGINE_STDCALL
#endif
