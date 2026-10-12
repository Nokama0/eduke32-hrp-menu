#pragma once
struct PackBox { int w, h; };
struct PackPos { short x, y; };

// Shelf-packs `n` boxes left to right, top to bottom, `pad` apart, IN INPUT ORDER: the glyph index is the position,
// so never sort. Zero-size boxes get {0,0}.
inline bool fontpack_shelf(const PackBox* boxes, int n, int atlasW, int atlasH, int pad,
                           PackPos* out) {
  if (!boxes || !out || n < 0 || atlasW <= 0 || atlasH <= 0 || pad < 0) return false;
  int penX = pad, penY = pad, rowH = 0;
  for (int i = 0; i < n; i++) {
    const int w = boxes[i].w, h = boxes[i].h;
    if (w <= 0 || h <= 0) { out[i].x = 0; out[i].y = 0; continue; }
    if (penX + w + pad > atlasW) { penX = pad; penY += rowH + pad; rowH = 0; }
    if (penX + w + pad > atlasW) return false;     // wider than the atlas itself
    if (penY + h + pad > atlasH) return false;
    out[i].x = (short)penX; out[i].y = (short)penY;
    penX += w + pad;
    if (h > rowH) rowH = h;
  }
  return true;
}

// The size ladder, smallest first. 512x256 is the baked atlas at 100%;
// 2048x2048 is the ceiling (past it, use the baked atlas).
static const PackBox FONTPACK_LADDER[6] =
  { {512,256}, {512,512}, {1024,512}, {1024,1024}, {2048,1024}, {2048,2048} };

// Index of the FIRST rung the boxes fit (out and chosen filled for that rung), or -1 when none does.
inline int fontpack_choose(const PackBox* boxes, int n, int pad, PackPos* out, PackBox* chosen) {
  for (int r = 0; r < 6; r++) {
    if (fontpack_shelf(boxes, n, FONTPACK_LADDER[r].w, FONTPACK_LADDER[r].h, pad, out)) {
      if (chosen) *chosen = FONTPACK_LADDER[r];
      return r;
    }
  }
  return -1;
}
