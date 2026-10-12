#pragma once

// Engine line-of-sight test, called not hooked. It MUTATES engine globals (clipsectorlist[], sectbitmap, and it can
// free and reallocate reachablesectors), so call ek_cansee only from the SwapBuffers hook; never from the scan worker.
bool cansee_scan(void);
bool cansee_ready(void);

unsigned cansee_rva(void);

// False when not armed: a caller that forgets to check gets "cannot see".
bool ek_cansee(int x1, int y1, int z1, int sect1,
               int x2, int y2, int z2, int sect2);
