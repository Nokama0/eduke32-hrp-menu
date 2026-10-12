#pragma once

// Enumerate every `call rel32` in the located G_DrawRooms, log what distinguishes each target, and name ONE site, the
// renderDrawMasks call the player's view uses, to patch so the world-quad list flushes on top of masked geometry.

// G_DrawRooms calls renderDrawMasks twice and only one is the player's.

// Step (b): over one target's two sites and the two renderDrawRoomsQ16 heads.
inline bool ekm_mask_straddles(unsigned a, unsigned b,
                               unsigned d1, unsigned d2) {
  const unsigned lo = a < b ? a : b;
  const unsigned hi = a < b ? b : a;
  return d1 < d2 && lo > d1 && lo < d2 && hi > d2;
}

// Step (d), the line that picks the player over the monitor: the player's view is the call after the SECOND head; 0
// when both or neither.
inline unsigned ekm_mask_player_site(unsigned a, unsigned b, unsigned d2) {
  const bool aAfter = a > d2;
  const bool bAfter = b > d2;
  if (aAfter == bAfter) return 0;
  return aAfter ? a : b;
}

// Step (b)'s size floor: renderDrawMasks measures 6937 bytes of .pdata (engine.cpp:10166-10440).
const unsigned EK_MASK_MIN_EXTENT = 4096u;

// Is the row table running out of room? mask_locate needs a COMPLETE table, so the cap is a feature kill; the log warns
// at a quarter left.
inline bool ekm_mask_table_is_tight(int found, int max) {
  return max > 0 && found > max - (max / 4);
}

// Step (e)'s floor: renderDrawMasks has 8 call sites in five translation units and measures 10 sites in 7 functions, so
// a target reached from one or two functions is not it.
const int EK_MASK_MIN_SITES = 5;
const int EK_MASK_MIN_OWNERS = 5;
inline bool ekm_mask_corroborated(int sites, int owners) {
  return sites >= EK_MASK_MIN_SITES && owners >= EK_MASK_MIN_OWNERS;
}

// Where the nearest call before `site` ends, 0 if none.
inline unsigned ekm_mask_prev_call_end(const unsigned* sites,
                                       const unsigned char* anchors,
                                       int n, unsigned site) {
  if (!sites || !anchors || n <= 0) return 0;
  unsigned best = 0;
  for (int i = 0; i < n; i++) {
    if (!anchors[i]) continue;
    const unsigned e = sites[i] + 5u;
    if (e <= site && e > best) best = e;
  }
  return best;
}

// How many of the sixteen bytes before a call are its own setup.
inline int ekm_mask_window_len(unsigned site, unsigned prevEnd) {
  const unsigned lo = site >= 16u ? site - 16u : 0u;
  const unsigned from = (prevEnd > lo && prevEnd <= site) ? prevEnd : lo;
  return (int)(site - from);
}

// Windows x64 argument registers: rcx(1) rdx(2) rsi(6) rdi(7) r8(8) r9(9); rsi/rdi are nonvolatile but the rule
// specified them.
inline bool ekm_mask_is_arg_reg(int r) {
  return r == 1 || r == 2 || r == 6 || r == 7 || r == 8 || r == 9;
}

// Opcodes whose destination is the ModRM REG field.
inline bool ekm_mask_op_reg_dest(unsigned char op) {
  return op == 0x03 || op == 0x0B || op == 0x23 || op == 0x2B ||
         op == 0x33 || op == 0x63 || op == 0x8B || op == 0x8D;
}

// Opcodes whose destination is ModRM R/M (a register only when mod is 11).
inline bool ekm_mask_op_rm_dest(unsigned char op) {
  return op == 0x01 || op == 0x09 || op == 0x21 || op == 0x29 ||
         op == 0x31 || op == 0x89 || op == 0x81 || op == 0x83 ||
         op == 0xC1 || op == 0xC7 || op == 0xD1 || op == 0xD3 ||
         op == 0xF7 || op == 0xFF;
}

// Which register a write at `p` lands in, or -1.
inline int ekm_mask_arg_dest(const unsigned char* b, int n, int p) {
  if (!b || p < 0 || p >= n) return -1;
  int rexR = 0, rexB = 0;
  if (b[p] >= 0x40 && b[p] <= 0x4F) {
    rexR = (b[p] >> 2) & 1;
    rexB = b[p] & 1;
    if (++p >= n) return -1;
  }
  const unsigned char op = b[p];
  if (op == 0x0F) {
    if (p + 2 >= n) return -1;
    const unsigned char op2 = b[p + 1];
    if (op2 != 0xB6 && op2 != 0xB7 && op2 != 0xBE && op2 != 0xBF) return -1;
    return (int)((b[p + 2] >> 3) & 7) + (rexR << 3);
  }
  if (p + 1 >= n) return -1;
  const unsigned char modrm = b[p + 1];
  if (ekm_mask_op_reg_dest(op)) return (int)((modrm >> 3) & 7) + (rexR << 3);
  if (ekm_mask_op_rm_dest(op) && (modrm >> 6) == 3)
    return (int)(modrm & 7) + (rexB << 3);
  return -1;
}

// Does the window write an argument register?
inline bool ekm_mask_touches_arg(const unsigned char* b, int n) {
  if (!b || n <= 0) return false;
  for (int p = 0; p < n; p++)
    if (ekm_mask_is_arg_reg(ekm_mask_arg_dest(b, n, p))) return true;
  return false;
}


// The table, then the locate; at most once, driven from gamestate.cpp's 1 Hz sampler after camera_scan publishes.
void maskscan_probe(void);

// Patch the located site.
void maskscan_install(void);

// Put the call back; the patch points into this module, so unloading must restore it.
void maskscan_remove(void);

// No "is there a draw point" accessor: ask worldquad_push, which maskscan_install/remove set via
// worldquad_set_drawpoint.
