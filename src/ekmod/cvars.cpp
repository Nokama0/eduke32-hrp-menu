// The cvar registry: named access to the engine's own globals. Offsets break on every rolling build, so each cvar's
// live pointer is found by NAME in the OSD table, its records by SHAPE (name-first search missed fov: pooled strings).

#include <windows.h>
#include "log.h"
#include "cvars.h"

static EkCvar s_cvars[EK_CVAR_MAX];
static int s_count = 0;
static bool s_scanned = false;

// Image bounds, so a pointer can be rejected before it is dereferenced.
static const unsigned char* s_base = NULL;
static size_t s_size = 0;

static bool in_image(const void* p) {
  const unsigned char* b = (const unsigned char*)p;
  return b >= s_base && b < s_base + s_size;
}

// A committed, readable page is not guaranteed just because an address is inside the image: section alignment leaves
// gaps.
static bool readable(const void* p, size_t bytes) {
  if (!in_image(p)) return false;
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
  if (mbi.State != MEM_COMMIT) return false;
  if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
  const unsigned char* end = (const unsigned char*)p + bytes;
  const unsigned char* regionEnd = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
  return end <= regionEnd;
}

// A cvar name is short, printable and identifier-shaped. Anything else is not
// a record we want, whatever else happens to line up.
static bool plausible_name(const char* s) {
  if (!readable(s, 1)) return false;
  int i = 0;
  for (; i < EK_CVAR_NAME_MAX; i++) {
    if (!readable(s + i, 1)) return false;
    const char c = s[i];
    if (c == 0) break;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return i > 0 && i < EK_CVAR_NAME_MAX;
}

static bool plausible_string(const char* s, int limit) {
  if (!readable(s, 1)) return false;
  for (int i = 0; i < limit; i++) {
    if (!readable(s + i, 1)) return false;
    if (s[i] == 0) return true;
  }
  return false;
}

// Exactly one type bit, drawn from the real set, and nothing above the
// documented flag range. This is the filter that does the work.
static bool plausible_flags(unsigned f) {
  const unsigned type = f & 0x3F;
  switch (type) {
    case EK_CVAR_FLOAT: case EK_CVAR_INT: case EK_CVAR_UINT:
    case EK_CVAR_BOOL:  case EK_CVAR_STRING: case EK_CVAR_DOUBLE: break;
    default: return false;
  }
  return (f & ~0x3FFFu) == 0;
}

bool cvars_scan(void) {
  if (s_scanned) return s_count > 0;
  s_scanned = true;

  const HMODULE host = GetModuleHandleA(NULL);
  s_base = (const unsigned char*)host;
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { ek_log("cvars: host is not a DOS image"); return false; }
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { ek_log("cvars: host is not a PE image"); return false; }
  s_size = nt->OptionalHeader.SizeOfImage;

  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const int nsec = nt->FileHeader.NumberOfSections;
  int scanned = 0;

  for (int i = 0; i < nsec && s_count < EK_CVAR_MAX; i++) {
    // WRITABLE WITH RAW DATA ONLY. The arrays are initialised with pointers so
    // they cannot be in .bss, and the engine sets CVAR_MODIFIED on them at
    // runtime so they cannot be in a read-only section either.
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
    if (sec[i].SizeOfRawData == 0) continue;

    const unsigned char* start = s_base + sec[i].VirtualAddress;
    const DWORD span = sec[i].SizeOfRawData < sec[i].Misc.VirtualSize
                         ? sec[i].SizeOfRawData : sec[i].Misc.VirtualSize;
    scanned++;

    for (DWORD off = 0; off + EK_CVAR_STRIDE <= span && s_count < EK_CVAR_MAX; off += 8) {
      const unsigned char* rec = start + off;
      if (!readable(rec, EK_CVAR_STRIDE)) continue;

      const char* const* pname = (const char* const*)rec;
      const char* name = pname[0];
      const char* desc = pname[1];
      void* ptr = (void*)pname[2];
      if (!in_image(name) || !in_image(desc) || !in_image(ptr)) continue;

      const unsigned flags = *(const unsigned*)(rec + 0x18);
      const int vmin = *(const int*)(rec + 0x1C);
      const int vmax = *(const int*)(rec + 0x20);
      if (!plausible_flags(flags)) continue;
      if (vmin > vmax) continue;
      if (!plausible_name(name)) continue;
      if (!plausible_string(desc, 512)) continue;

      EkCvar& c = s_cvars[s_count++];
      c.name = name; c.desc = desc; c.ptr = ptr;
      c.flags = flags; c.vmin = vmin; c.vmax = vmax;
    }
  }

  ek_log("cvars: %d record(s) across %d writable section(s)", s_count, scanned);
  if (s_count >= EK_CVAR_MAX)
    ek_log("cvars: TABLE FULL at %d -- raise EK_CVAR_MAX, some cvars are missing", EK_CVAR_MAX);
  return s_count > 0;
}

int cvars_count(void) { return s_count; }
const EkCvar* cvars_at(int i) { return (i >= 0 && i < s_count) ? &s_cvars[i] : NULL; }

const EkCvar* cvars_find(const char* name) {
  if (!name) return NULL;
  const EkCvar* hit = NULL;
  for (int i = 0; i < s_count; i++) {
    if (strcmp(s_cvars[i].name, name) != 0) continue;
    // AMBIGUITY IS REPORTED, NOT RESOLVED. Two records under one name means the shape filter caught something it should
    // not have, and picking either one would hand back an address nobody verified.
    if (hit) { ek_log("cvars: '%s' is ambiguous -- refusing to guess", name); return NULL; }
    hit = &s_cvars[i];
  }
  return hit;
}

bool cvar_get_int(const char* name, int* out) {
  const EkCvar* c = cvars_find(name);
  if (!c || !out) return false;
  const unsigned type = c->flags & 0x3F;
  if (type != EK_CVAR_INT && type != EK_CVAR_UINT && type != EK_CVAR_BOOL) return false;
  if (!readable(c->ptr, sizeof(int))) return false;
  *out = *(const int*)c->ptr;
  return true;
}

// A committed, writable page is not guaranteed just because an address is inside the image: section alignment leaves
// gaps, and the cvar's `ptr` came out of a record this file found by SHAPE.
static bool writable(const void* p, size_t bytes) {
  if (!in_image(p)) return false;
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

// GUARDED, because a faulted write must cost the ROW and not the game; MSVC cannot mix SEH with C++ unwinding.
static bool store_int(void* p, int v) {
  __try {
    *(int*)p = v;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

bool cvar_set_int(const char* name, int value, int* applied) {
  const EkCvar* c = cvars_find(name);
  if (!cvar_row_available(c)) return false;
  if (!writable(c->ptr, sizeof(int))) {
    ek_log("cvars: '%s' is not writable at %p -- the row is off for this session", name, c->ptr);
    return false;
  }
  const int v = cvar_clamped(*c, value);
  if (!store_int(c->ptr, v)) {
    ek_log("cvars: writing '%s' at %p faulted; nothing was changed", name, c->ptr);
    return false;
  }
  // CVAR_MODIFIED IS NOT SET: the engine already saves any value that differs from the default (osd.cpp:105-116).
  if (applied) *applied = v;
  return true;
}
