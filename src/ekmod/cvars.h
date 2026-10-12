#pragma once

// EDuke32 OSD cvar registry (osdcvardata_t x64, 0x28 bytes): +0 name, +8 desc, +0x10 ptr (live variable), +0x18 flags,
// +0x1C min, +0x20 max.
const int EK_CVAR_STRIDE   = 0x28;
const int EK_CVAR_NAME_MAX = 64;

// r10669 has 204; the scan logs if the table fills.
const int EK_CVAR_MAX = 1024;

const unsigned EK_CVAR_FLOAT  = 0x01;
const unsigned EK_CVAR_INT    = 0x02;
const unsigned EK_CVAR_UINT   = 0x04;
const unsigned EK_CVAR_BOOL   = 0x08;
const unsigned EK_CVAR_STRING = 0x10;
const unsigned EK_CVAR_DOUBLE = 0x20;

struct EkCvar {
  const char* name;
  const char* desc;
  void* ptr;
  unsigned flags;
  int vmin, vmax;
};

bool cvars_scan(void);

int cvars_count(void);
const EkCvar* cvars_at(int i);

// Also NULL when two records claim the name.
const EkCvar* cvars_find(const char* name);

bool cvar_get_int(const char* name, int* out);

// Type is the low six bits; cl_weaponswitch also carries CVAR_MULTI (0x400).
inline bool cvar_type_writable(unsigned flags) {
  const unsigned type = flags & 0x3F;
  return type == EK_CVAR_INT || type == EK_CVAR_UINT || type == EK_CVAR_BOOL;
}

// The record's vmin/vmax is the authority on range (as the engine's OSD clamps).
inline int cvar_clamped(const EkCvar& c, int v) {
  if (v < c.vmin) return c.vmin;
  if (v > c.vmax) return c.vmax;
  return v;
}

// NULL covers absent and ambiguous alike.
inline bool cvar_row_available(const EkCvar* c) {
  return c != 0 && cvar_type_writable(c->flags);
}

// Clamped to the record's vmin/vmax; *applied gets the stored value. Same write the engine's OSD does
// (osd.cpp:2282-2325).
bool cvar_set_int(const char* name, int value, int* applied = 0);
