#pragma once

// The two firing functions, located and hooked.
// Render thread only: it patches live code.
bool gamehooks_install(void);

// From DllMain's DLL_PROCESS_DETACH, so an unloaded mod leaves no patch in a
// game that keeps running.
void gamehooks_remove(void);

// A_ShootWithZvel / P_PreFireHitscan are hooked and their trampolines are live.
bool gamehooks_shoot_ready(void);
bool gamehooks_spread_ready(void);

unsigned gamehooks_bullets_fired(void);

unsigned gamehooks_shoot_rva(void);
unsigned gamehooks_spread_rva(void);

// A_IncurDamage's entry rva, or 0 when the signature did not resolve to exactly one function.
unsigned gamehooks_incurdamage_rva(void);
