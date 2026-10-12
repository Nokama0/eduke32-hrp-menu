// Finding tilesiz[] by tying it to something proven: MOST live sprite picnums must have a non-zero size. A zeroed
// region passes any range test on small numbers but fails all of these.

#include <windows.h>
#include "log.h"
#include "gamestate.h"
#include "tiles.h"

static const unsigned char* volatile s_tiles = NULL;
static volatile LONG s_rva = 0;
static volatile LONG s_nonzero = 0;

static const unsigned char* s_base = NULL;
static size_t s_size = 0;

struct Region { const unsigned char* base; size_t size; };
static Region s_regions[1024];
static int s_regionCount = 0;

static void enumerate_regions(void) {
  s_regionCount = 0;
  const unsigned char* p = s_base;
  const unsigned char* const end = s_base + s_size;
  const int cap = (int)(sizeof(s_regions) / sizeof(s_regions[0]));
  while (p < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
    if (mbi.RegionSize == 0) break;
    const unsigned char* rbase = (const unsigned char*)mbi.BaseAddress;
    if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
      Region* last = s_regionCount ? &s_regions[s_regionCount - 1] : NULL;
      if (last && last->base + last->size == rbase) last->size += mbi.RegionSize;
      else if (s_regionCount < cap) {
        s_regions[s_regionCount].base = rbase;
        s_regions[s_regionCount].size = mbi.RegionSize;
        s_regionCount++;
      }
    }
    p = rbase + mbi.RegionSize;
  }
}

static bool in_region(const void* p, size_t bytes) {
  const unsigned char* b = (const unsigned char*)p;
  for (int i = 0; i < s_regionCount; i++)
    if (b >= s_regions[i].base && b + bytes <= s_regions[i].base + s_regions[i].size)
      return true;
  return false;
}

// The picnums the engine is currently drawing. Collected from the verified
// sprite[] so the test below rests on measured data, not on assumption.
static const int kMaxWanted = 256;
static int collect_live_picnums(int* out) {
  int n = 0;
  for (int i = 0; i < EK_MAXSPRITES && n < kMaxWanted; i++) {
    const unsigned char* r = gamestate_sprite(i);
    if (!r) break;
    const short st = *(const short*)(r + EK_SPR_STATNUM);
    if (st < 0 || st >= EK_MAXSTATUS) continue;          // free slot
    if (r[EK_SPR_XREPEAT] == 0 || r[EK_SPR_YREPEAT] == 0) continue;  // not drawn
    const short pic = *(const short*)(r + EK_SPR_PICNUM);
    if (pic <= 0 || pic >= EK_MAXTILES_ARR) continue;
    bool known = false;
    for (int k = 0; k < n; k++) if (out[k] == pic) { known = true; break; }
    if (!known) out[n++] = pic;
  }
  return n;
}

// Picnum test FIRST, load-bearing: it kills almost every candidate in a few dozen reads; sweeping all 30720 entries per
// offset is ~150 billion.
static bool candidate_ok(const unsigned char* arr, const int* pics, int npics,
                         int* nonzeroOut, int* hitsOut) {
  // A MAJORITY, NOT ALL: the engine holds sprites with 0x0 tiles and skips them, so one such sprite must not reject
  // the real array.
  int hits = 0;
  for (int k = 0; k < npics; k++) {
    const unsigned char* e = arr + (size_t)pics[k] * EK_TILESIZ_STRIDE;
    const short w = *(const short*)e;
    const short h = *(const short*)(e + 2);
    if (w < 0 || h < 0 || w > EK_TILE_MAX_DIM || h > EK_TILE_MAX_DIM) return false;
    if (w > 0 && h > 0) hits++;
  }
  if (hits * 100 < npics * 80) return false;          // four in five must have a size
  if (hitsOut) *hitsOut = hits;

  // Only now the full sweep: no entry out of range anywhere in the array, and
  // enough of it populated to be a real tile table.
  int nonzero = 0;
  for (int i = 0; i < EK_MAXTILES_ARR; i++) {
    const short w = *(const short*)(arr + (size_t)i * EK_TILESIZ_STRIDE);
    const short h = *(const short*)(arr + (size_t)i * EK_TILESIZ_STRIDE + 2);
    if (w < 0 || h < 0 || w > EK_TILE_MAX_DIM || h > EK_TILE_MAX_DIM) return false;
    if (w || h) nonzero++;
  }
  // MEASURED, NOT GUESSED. Reading stock DUKE3D.GRP's art manifest directly gives 3546 non-zero tiles across slots
  // 0..5119; the floor sits well under that for add-on GRPs.
  if (nonzero < 1000) return false;

  if (nonzeroOut) *nonzeroOut = nonzero;
  return true;
}

bool tiles_scan(void) {
  if (s_tiles) return true;
  if (!gamestate_ready()) return false;      // the validator needs live sprites

  s_base = (const unsigned char*)GetModuleHandleA(NULL);
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  s_size = nt->OptionalHeader.SizeOfImage;
  enumerate_regions();

  int pics[kMaxWanted];
  const int npics = collect_live_picnums(pics);
  if (npics < 8) {
    ek_log("tiles: only %d live picnum(s); too few to identify tilesiz safely", npics);
    return false;
  }

  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const int nsec = nt->FileHeader.NumberOfSections;
  // 914 candidates passed on the first live run, probably shifted views of one array: the span of matches is recorded.
  int matches = 0, bestNonzero = 0, bestHits = 0;
  unsigned firstRva = 0, lastRva = 0;
  const unsigned char* best = NULL;
  unsigned bestRva = 0;

  for (int i = 0; i < nsec; i++) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
    const unsigned char* start = s_base + sec[i].VirtualAddress;
    const DWORD span = sec[i].Misc.VirtualSize;
    if (span < EK_TILESIZ_BYTES) continue;

    for (DWORD off = 0; off + EK_TILESIZ_BYTES <= span; off += 4) {
      const unsigned char* cand = start + off;
      // GCC aligns a 120KB global array to at least 16, and requiring it drops
      // three of every four shifted look-alikes at no cost to the real answer.
      if ((((UINT_PTR)cand) & 15u) != 0) continue;
      if (!in_region(cand, EK_TILESIZ_BYTES)) continue;
      int nonzero = 0, hits = 0;
      if (!candidate_ok(cand, pics, npics, &nonzero, &hits)) continue;
      matches++;
      const unsigned thisRva = (unsigned)(sec[i].VirtualAddress + off);
      if (!firstRva) firstRva = thisRva;
      lastRva = thisRva;
      // Ranked on the live-picnum agreement first and density only to
      // break ties: the picnums are evidence from measured data, the
      // count is a property of the array shape.
      if (hits > bestHits || (hits == bestHits && nonzero > bestNonzero)) {
        bestHits = hits; bestNonzero = nonzero; best = cand;
        bestRva = (unsigned)(sec[i].VirtualAddress + off);
      }
    }
  }

  if (!best) {
    ek_log("tiles: tilesiz not found (checked against %d live picnum(s))", npics);
    return false;
  }
  // AMBIGUITY IS REPORTED. More than one region satisfying hundreds of
  // constraints is surprising, and surprising is worth a line in the log rather
  // than a silent pick.
  if (matches > 1) {
    const unsigned span = lastRva - firstRva;
    if (span <= (unsigned)EK_TILESIZ_BYTES)
      ek_log("tiles: %d candidates, all within 0x%X bytes (rva 0x%07X..0x%07X) -- "
             "one array seen at shifted offsets, not an ambiguity",
             matches, span, firstRva, lastRva);
    else
      ek_log("tiles: WARNING -- %d candidates spanning 0x%X bytes "
             "(rva 0x%07X..0x%07X). That is wider than one array: the densest was "
             "taken but this is a genuine ambiguity, not shifted views.",
             matches, span, firstRva, lastRva);
  }

  InterlockedExchange(&s_rva, (LONG)bestRva);
  InterlockedExchange(&s_nonzero, (LONG)bestNonzero);
  s_tiles = best;
  ek_log("tiles: tilesiz at rva 0x%07X, %d non-zero tile(s); %d of %d live "
         "picnum(s) have a size", bestRva, bestNonzero, bestHits, npics);
  return true;
}

bool tiles_ready(void) { return s_tiles != NULL; }
unsigned tiles_rva(void) { return (unsigned)InterlockedCompareExchange(&s_rva, 0, 0); }
int tiles_nonzero(void) { return (int)InterlockedCompareExchange(&s_nonzero, 0, 0); }

void tile_size(int picnum, int* w, int* h) {
  if (w) *w = 0;
  if (h) *h = 0;
  const unsigned char* const t = s_tiles;
  if (!t || picnum < 0 || picnum >= EK_MAXTILES_ARR) return;
  const unsigned char* e = t + (size_t)picnum * EK_TILESIZ_STRIDE;
  if (w) *w = *(const short*)e;
  if (h) *h = *(const short*)(e + 2);
}
