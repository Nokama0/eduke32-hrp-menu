#pragma once

// Naturally aligned, not packed: 40 bytes, flags at +32 on x64 (x86's 24/+16 misreads defproj as flags).
const int EK_TILEDATA_STRIDE = 40;
const int EK_TILEDATA_FLAGS  = 32;

const unsigned EK_SFLAG_BADGUY           = 0x00000020u;
const unsigned EK_SFLAG_HARDCODED_BADGUY = 0x00040000u;
const unsigned EK_SFLAG_ANY_BADGUY = EK_SFLAG_BADGUY | EK_SFLAG_HARDCODED_BADGUY;

bool badguy_scan(void);
bool badguy_ready(void);
unsigned badguy_rva(void);
int badguy_count(void);

// False when g_tile is not located; callers decide draw-all or draw-none.
bool picnum_is_enemy(int picnum);

// Engine-flagged enemies that are no threat (RAT); our preference.
bool picnum_is_harmless_critter(int picnum);

const unsigned char* badguy_tile_record(int picnum);
