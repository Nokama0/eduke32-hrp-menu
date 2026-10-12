// Verbatim from Megaton's fontgen.h; keep identical.
#pragma once
// GDI only, so not linkable into test_host; the packing rule is in fontpack.h.
#include "font.h"

struct FontAtlas {
  int w, h;
  unsigned char* pixels;
  Glyph glyphs[FONT_COUNT][96];
  int height[FONT_COUNT];
  int pct;
};

// All or nothing: any failure returns false, `*out` untouched, baked atlas stays. `out` must be zeroed or previously
// built.
bool fontgen_build(int pct, FontAtlas* out);

void fontgen_free(FontAtlas* a);
