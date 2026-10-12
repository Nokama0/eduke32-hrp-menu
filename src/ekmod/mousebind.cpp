// Locates localInput (input_t, 20 bytes, global.h:89), the record the tic consumes, and ORs a thumb button's action bit
// into it. No registry or symbol exists for it, so a signature scan on the instruction that stores to it (20
// mostly-zero bytes in a 21 MB .bss have nothing to recognise by content).

#include <windows.h>
#include "log.h"
#include "mousebind.h"
#include "config.h"
#include "input.h"

// P_GetInput's inlined early-out, 42 bytes at rva 0x4b3ad in the r10669-ec5824db8 build: three zeroing movs to [rip+d], then mov
// eax,[rax]; shl eax,26 (SK_GAMEQUIT, which also fixes the macros.h action-bit numbering); mov [rip+d],eax. Occurs once
// image-wide.
static const unsigned char EK_LOCALINPUT_SIG[] = {
  0x48, 0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
  0x8B, 0x00,
  0xC1, 0xE0, 0x1A,
  0x89, 0x05, 0x00, 0x00, 0x00, 0x00
};

// 1 where the byte must match, 0 at a displacement.
static const unsigned char EK_LOCALINPUT_MASK[] = {
  1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
  1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
  1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
  1, 1,
  1, 1, 1,
  1, 1, 0, 0, 0, 0
};

static const size_t EK_LOCALINPUT_LEN = sizeof(EK_LOCALINPUT_SIG);
static const DWORD EK_LOCALINPUT_DISP_AT = 38;  // Last displacement: localInput = matchRva + 42 + disp32, i.e. `.bits`.

// Same length, or the scan reads past the mask on the last byte.
static_assert(sizeof(EK_LOCALINPUT_MASK) == sizeof(EK_LOCALINPUT_SIG),
              "the localInput signature and its wildcard mask must match in length");
static_assert(EK_LOCALINPUT_DISP_AT + 4 <= sizeof(EK_LOCALINPUT_SIG),
              "the displacement must lie inside the signature");

// Only the first four bytes are written; the whole struct is bounds-checked.
static const size_t EK_INPUT_T_SIZE = 20;

static unsigned* volatile s_bits = NULL;
static volatile LONG s_rva = 0;

// Permanent: the image does not change, so retrying only fills the log.
static bool s_refused = false;
static bool s_attempted = false;

struct SigHit { unsigned rva; int count; int faulted; };

// Masked pass over executable sections, guarded (VirtualSize can exceed what is committed). A parameterised copy of
// cansee.cpp's scan, which is proven on hardware, so not merged.
static bool find_pattern(const unsigned char* base, SigHit* out) {
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const int nsec = (int)nt->FileHeader.NumberOfSections;
  const size_t len = EK_LOCALINPUT_LEN;

  out->rva = 0;
  out->count = 0;
  out->faulted = 0;

  for (int i = 0; i < nsec; i++) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    DWORD span = sec[i].Misc.VirtualSize;
    if (span > sec[i].SizeOfRawData && sec[i].SizeOfRawData != 0) span = sec[i].SizeOfRawData;
    if (span < (DWORD)len) continue;
    const unsigned char* const start = base + sec[i].VirtualAddress;

    __try {
      const DWORD last = span - (DWORD)len;
      for (DWORD off = 0; off <= last; off++) {
        // First byte rejects most offsets in a five-megabyte section.
        if (start[off] != EK_LOCALINPUT_SIG[0]) continue;
        size_t k = 1;
        for (; k < len; k++) {
          if (!EK_LOCALINPUT_MASK[k]) continue;
          if (start[off + k] != EK_LOCALINPUT_SIG[k]) break;
        }
        if (k != len) continue;
        if (out->count == 0) out->rva = (unsigned)(sec[i].VirtualAddress + off);
        out->count++;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      // A section that faulted was not searched: a second match could sit unseen, which the refusal below must catch.
      out->faulted = 1;
    }
  }
  return true;
}

// Zero means the match was not inside a function.
static unsigned entry_containing(const unsigned char* base, unsigned rva) {
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY* dir =
      &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
  if (dir->VirtualAddress == 0 || dir->Size < sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY))
    return 0;

  unsigned found = 0;
  __try {
    const IMAGE_RUNTIME_FUNCTION_ENTRY* fn =
        (const IMAGE_RUNTIME_FUNCTION_ENTRY*)(base + dir->VirtualAddress);
    const DWORD n = dir->Size / (DWORD)sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY);
    for (DWORD i = 0; i < n; i++) {
      if (fn[i].EndAddress <= fn[i].BeginAddress) continue;
      if (rva < fn[i].BeginAddress || rva >= fn[i].EndAddress) continue;
      found = fn[i].BeginAddress;
      break;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    found = 0;
  }
  return found;
}

// A displacement into code or read-only data did not resolve to a global.
static bool in_writable_data(const unsigned char* base, unsigned rva, size_t bytes) {
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const int nsec = (int)nt->FileHeader.NumberOfSections;
  for (int i = 0; i < nsec; i++) {
    const DWORD lo = sec[i].VirtualAddress;
    const DWORD hi = lo + sec[i].Misc.VirtualSize;
    if (rva < lo || rva + (DWORD)bytes > hi) continue;
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) return false;
    if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) return false;
    return true;
  }
  return false;
}

// Section alignment leaves gaps; VirtualQuery asks without faulting.
static bool writable(const void* p, size_t bytes) {
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
  if (mbi.State != MEM_COMMIT) return false;
  if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
  const DWORD ok = PAGE_READWRITE | PAGE_WRITECOPY |
                   PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  if (!(mbi.Protect & ok)) return false;
  const unsigned char* end = (const unsigned char*)p + bytes;
  const unsigned char* regionEnd = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
  return end <= regionEnd;
}

static void locate_once(void) {
  const unsigned char* const base = (const unsigned char*)GetModuleHandleA(NULL);
  if (!base) { s_refused = true; ek_log("mousebind: no host module"); return; }

  SigHit hit;
  if (!find_pattern(base, &hit)) {
    s_refused = true;
    ek_log("mousebind: the host module has no PE headers; refusing");
    return;
  }

  if (hit.count == 0 && !hit.faulted) {
    s_refused = true;
    ek_log("mousebind: signature not found in this build -- the thumb-button "
           "rows stay unavailable and nothing is written");
    return;
  }

  // Refuse rather than guess: a second match means the pattern did not discriminate.
  if (hit.count != 1 || hit.faulted) {
    s_refused = true;
    if (hit.faulted)
      ek_log("mousebind: a code section faulted mid-scan, so uniqueness cannot "
             "be claimed (%d match(es) before the fault); refusing to guess",
             hit.count);
    else
      ek_log("mousebind: signature is ambiguous -- %d matches, first at rva "
             "0x%07X; refusing to guess", hit.count, hit.rva);
    return;
  }

  const unsigned entry = entry_containing(base, hit.rva);
  if (entry == 0 || entry > hit.rva) {
    s_refused = true;
    ek_log("mousebind: match at rva 0x%07X is not inside any function in the "
           "exception directory; refusing", hit.rva);
    return;
  }

  // Relative to the end of the instruction, i.e. the pattern end.
  int disp = 0;
  __try {
    const unsigned char* const d = base + hit.rva + EK_LOCALINPUT_DISP_AT;
    disp = (int)((unsigned)d[0] | ((unsigned)d[1] << 8) |
                 ((unsigned)d[2] << 16) | ((unsigned)d[3] << 24));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    s_refused = true;
    ek_log("mousebind: could not read the displacement at rva 0x%07X; refusing",
           hit.rva + EK_LOCALINPUT_DISP_AT);
    return;
  }

  const long long target = (long long)hit.rva + (long long)EK_LOCALINPUT_LEN + disp;
  if (target < 0 || target > 0x7FFFFFFF ||
      !in_writable_data(base, (unsigned)target, EK_INPUT_T_SIZE)) {
    s_refused = true;
    ek_log("mousebind: the store at rva 0x%07X resolves to 0x%llX, which is not "
           "writable image data; refusing", hit.rva, target);
    return;
  }

  unsigned* const bits = (unsigned*)(void*)(base + (unsigned)target);
  if (!writable(bits, EK_INPUT_T_SIZE)) {
    s_refused = true;
    ek_log("mousebind: rva 0x%07X is not committed and writable; refusing",
           (unsigned)target);
    return;
  }

  InterlockedExchange(&s_rva, (LONG)target);
  InterlockedExchangePointer((void* volatile*)&s_bits, (void*)bits);
  ek_log("mousebind: localInput at rva 0x%07X -- one match at rva 0x%07X inside "
         "the function at rva 0x%07X (%+d), displacement %+d",
         (unsigned)target, hit.rva, entry, (int)(hit.rva - entry), disp);
}

// Guarded: a wrong derived address must cost the feature, not the game. A fault disarms permanently.
static void or_bits(unsigned* bits, unsigned mask) {
  __try {
    *bits |= mask;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    InterlockedExchangePointer((void* volatile*)&s_bits, (void*)NULL);
    s_refused = true;
    ek_log("mousebind: writing localInput at rva 0x%07X faulted; the rows are "
           "unavailable for the rest of this session", (unsigned)s_rva);
  }
}

static void ensure_located(void) {
  if (s_attempted) return;
  s_attempted = true;
  locate_once();
}

bool mousebind_or_bits(unsigned mask) {
  ensure_located();
  unsigned* const bits = s_bits;
  if (!bits || !mask) return false;
  or_bits(bits, mask);
  // or_bits disarms on a fault, so this says whether the write landed.
  return s_bits != NULL;
}

void mousebind_frame(void) {
  ensure_located();

  unsigned* const bits = s_bits;
  if (!bits) return;

  // Not while the panel is open: the player is clicking rows.
  if (input_panel_open()) return;

  const unsigned mask = mousebind_bits(g_cfg.mouse4action, input_thumb_down(0),
                                       g_cfg.mouse5action, input_thumb_down(1));
  // No clearing needed: the engine clears the record every tic (game.cpp:6542) and nothing touches it between
  // P_GetInput (:6485) and our hook (:6508). Both rows Off: mask 0, never touched.
  if (!mask) return;

  static int s_saidOnce = 0;
  if (!s_saidOnce && g_cfg.debuglog) {
    s_saidOnce = 1;
    ek_log("mousebind: first write -- mouse 4 = %s, mouse 5 = %s, mask 0x%08X",
           mousebind_action_name(g_cfg.mouse4action),
           mousebind_action_name(g_cfg.mouse5action), mask);
  }

  or_bits(bits, mask);
}

bool mousebind_ready(void) { return s_bits != NULL; }

unsigned mousebind_rva(void) { return (unsigned)s_rva; }
