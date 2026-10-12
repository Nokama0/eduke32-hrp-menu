#pragma once

const int EK_MAXSPRITES = 16384;
const int EK_MAXSECTORS = 4096;
const int EK_MAXSTATUS  = 1024;
const int EK_MAXTILES   = 30720;

// spritetype is 44 bytes (buildtypes.h), same offsets as World Tour; no pointers, so x64 does not shift it.
const int EK_SPR_STRIDE  = 44;
const int EK_SPR_X       = 0x00;
const int EK_SPR_Y       = 0x04;
const int EK_SPR_Z       = 0x08;
const int EK_SPR_CSTAT   = 0x0C;
const int EK_SPR_PICNUM  = 0x0E;
// shade int8 at 0x10, pal after it (buildtypes.h:209).
const int EK_SPR_PAL     = 0x11;
const int EK_SPR_XREPEAT = 0x14;
const int EK_SPR_YREPEAT = 0x15;
// Signed bytes (buildtypes.h:211): read as signed char.
const int EK_SPR_XOFFSET = 0x16;
const int EK_SPR_YOFFSET = 0x17;
const int EK_SPR_SECTNUM = 0x18;
const int EK_SPR_STATNUM = 0x1A;
const int EK_SPR_ANG     = 0x1C;
const int EK_SPR_OWNER   = 0x1E;
const int EK_SPR_XVEL    = 0x20;
const int EK_SPR_LOTAG   = 0x26;
const int EK_SPR_HITAG   = 0x28;
const int EK_SPR_EXTRA   = 0x2A;

const int EK_STAT_ACTOR  = 1;
const int EK_STAT_ZOMBIE = 2;
// Crack sprites live here (game.cpp:3475).
const int EK_STAT_STANDABLE = 6;
// The exit button lives on MISC (game.cpp:1647); other switches stay on 0.
const int EK_STAT_DEFAULT = 0;
const int EK_STAT_MISC    = 5;

const int EK_SPRITE_SCAN_DEPTH = 2048;

bool gamestate_scan(void);
bool gamestate_ready(void);

// Rva of the engine's `sprite` pointer slot, logged as evidence.
unsigned gamestate_sprite_ptr_rva(void);

// Rva of the records; zero until an array is in use.
unsigned gamestate_sprite_array_rva(void);

// The image walk decides, the content scan only witnesses: camera.cpp decodes the engine's own sprite load, while the
// scan is a heuristic that once picked another slot for the same array.
enum {
  EK_SPR_AUTH_WAITING = 0,
  EK_SPR_AUTH_IMAGE   = 1,
  EK_SPR_AUTH_REFUSED = 2
};
enum {
  EK_SPR_SRC_NONE  = 0,
  EK_SPR_SRC_IMAGE = 1,
  EK_SPR_SRC_SCAN  = 2
};

// rva of the `sprite` pointer, or 0 if the walk refused.
void gamestate_sprite_authority(unsigned spritePtrRva);

// Attempts (2 s) before a silent walk stops holding the array up.
const int EK_SPRITE_AUTHORITY_WAIT = 30;

inline int ekm_sprite_source_pick(int authState, bool imageOk, bool scanOk,
                                  bool waitedOut) {
  if (authState == EK_SPR_AUTH_IMAGE)
    // No usable slot means no level yet; never fall back to the scan's differing answer.
    return imageOk ? EK_SPR_SRC_IMAGE : EK_SPR_SRC_NONE;
  if (authState == EK_SPR_AUTH_WAITING && !waitedOut) return EK_SPR_SRC_NONE;
  return scanOk ? EK_SPR_SRC_SCAN : EK_SPR_SRC_NONE;
}

// Raw record, or NULL if out of range or unreadable. Read fields with the
// EK_SPR_* offsets above.
const unsigned char* gamestate_sprite(int i);

// Sprites the engine currently has in play (statnum below MAXSTATUS).
int gamestate_live_sprites(void);

// One scan thread only; cheap and idempotent.
void gamestate_regions_refresh(void);
// Range wholly inside one merged committed span.
bool gamestate_region_contains(const void* p, size_t bytes);
int  gamestate_region_count(void);
bool gamestate_region_at(int i, const unsigned char** base, size_t* bytes);

// The host executable's image, for a scan that needs to walk its sections.
const unsigned char* gamestate_module_base(void);
size_t gamestate_module_size(void);
