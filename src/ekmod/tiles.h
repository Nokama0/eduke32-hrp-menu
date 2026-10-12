#pragma once

// tilesiz[]: tile pixel sizes, turning xrepeat/yrepeat into world size.
const int EK_MAXTILES_ARR = 30720;          // build.h MAXTILES
const int EK_TILESIZ_STRIDE = 4;            // two int16: x then y
const size_t EK_TILESIZ_BYTES = (size_t)EK_MAXTILES_ARR * EK_TILESIZ_STRIDE;

const int EK_TILE_MAX_DIM = 1024;

bool tiles_scan(void);
bool tiles_ready(void);

// 0,0 if unknown: never size from it.
void tile_size(int picnum, int* w, int* h);
unsigned tiles_rva(void);
int tiles_nonzero(void);
