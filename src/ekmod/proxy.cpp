// version.dll proxy, the load vector. EDuke32 ships no DLLs and only five of its 17 imports are not KnownDLLs (version,
// dsound, winmm, wsock32, comctl32). version.dll imports three functions and loads at start. If Windows routes it via
// the apiset schema our copy is never read; dsound.dll is the fallback.

#include <windows.h>
#include <stdio.h>
#include "log.h"
#include "iat.h"
#include "gamehooks.h"
#include "camera.h"
#include "shot.h"
#include "maskscan.h"
#include "hostcheck.h"

// Resolved lazily: LoadLibrary under the loader lock can deadlock.
static volatile HMODULE s_real = NULL;

static HMODULE real_version(void) {
  HMODULE m = (HMODULE)InterlockedCompareExchangePointer((PVOID volatile*)&s_real, NULL, NULL);
  if (m) return m;

  // By absolute path: a bare name finds this very file and every forward recurses.
  char sys[MAX_PATH];
  const UINT n = GetSystemDirectoryA(sys, MAX_PATH);
  if (n == 0 || n >= MAX_PATH - 16) return NULL;
  strcat_s(sys, MAX_PATH, "\\version.dll");

  m = LoadLibraryA(sys);
  if (!m) return NULL;

  const HMODULE prev = (HMODULE)InterlockedCompareExchangePointer((PVOID volatile*)&s_real, m, NULL);
  if (prev) { FreeLibrary(m); return prev; }
  return m;
}

static FARPROC real_proc(const char* name) {
  const HMODULE m = real_version();
  return m ? GetProcAddress(m, name) : NULL;
}

// Typed wrappers: x64 MSVC has no __declspec(naked), and a wrong prototype corrupts the call.
#define FORWARD(ret, name, params, args, fail)                       \
  extern "C" ret WINAPI name params {                                \
    typedef ret(WINAPI * fn_t) params;                               \
    static fn_t fn = NULL;                                           \
    if (!fn) fn = (fn_t)real_proc(#name);                            \
    if (!fn) return fail;                                            \
    return fn args;                                                  \
  }

FORWARD(BOOL, GetFileVersionInfoA,
        (LPCSTR f, DWORD h, DWORD len, LPVOID data), (f, h, len, data), FALSE)
FORWARD(BOOL, GetFileVersionInfoW,
        (LPCWSTR f, DWORD h, DWORD len, LPVOID data), (f, h, len, data), FALSE)
FORWARD(BOOL, GetFileVersionInfoExA,
        (DWORD fl, LPCSTR f, DWORD h, DWORD len, LPVOID data), (fl, f, h, len, data), FALSE)
FORWARD(BOOL, GetFileVersionInfoExW,
        (DWORD fl, LPCWSTR f, DWORD h, DWORD len, LPVOID data), (fl, f, h, len, data), FALSE)
FORWARD(DWORD, GetFileVersionInfoSizeA, (LPCSTR f, LPDWORD h), (f, h), 0)
FORWARD(DWORD, GetFileVersionInfoSizeW, (LPCWSTR f, LPDWORD h), (f, h), 0)
FORWARD(DWORD, GetFileVersionInfoSizeExA, (DWORD fl, LPCSTR f, LPDWORD h), (fl, f, h), 0)
FORWARD(DWORD, GetFileVersionInfoSizeExW, (DWORD fl, LPCWSTR f, LPDWORD h), (fl, f, h), 0)
FORWARD(BOOL, VerQueryValueA,
        (LPCVOID b, LPCSTR sub, LPVOID* out, PUINT len), (b, sub, out, len), FALSE)
FORWARD(BOOL, VerQueryValueW,
        (LPCVOID b, LPCWSTR sub, LPVOID* out, PUINT len), (b, sub, out, len), FALSE)
// Parameters avoid the name `fn`, which the macro declares itself.
FORWARD(DWORD, VerFindFileA,
        (DWORD fl, LPCSTR nm, LPCSTR wd, LPCSTR ad, LPSTR cur, PUINT curlen, LPSTR dst, PUINT dstlen),
        (fl, nm, wd, ad, cur, curlen, dst, dstlen), 0)
FORWARD(DWORD, VerFindFileW,
        (DWORD fl, LPCWSTR nm, LPCWSTR wd, LPCWSTR ad, LPWSTR cur, PUINT curlen, LPWSTR dst, PUINT dstlen),
        (fl, nm, wd, ad, cur, curlen, dst, dstlen), 0)
FORWARD(DWORD, VerInstallFileA,
        (DWORD fl, LPCSTR sfn, LPCSTR dfn, LPCSTR sd, LPCSTR dd, LPCSTR cd, LPSTR tf, PUINT tflen),
        (fl, sfn, dfn, sd, dd, cd, tf, tflen), 0)
FORWARD(DWORD, VerInstallFileW,
        (DWORD fl, LPCWSTR sfn, LPCWSTR dfn, LPCWSTR sd, LPCWSTR dd, LPCWSTR cd, LPWSTR tf, PUINT tflen),
        (fl, sfn, dfn, sd, dd, cd, tf, tflen), 0)

// Undocumented: no public prototype. Four pointer args reach RCX/RDX/R8/R9, so arity above four reads stack garbage.
// Nothing observed calls it.
FORWARD(BOOL, GetFileVersionInfoByHandle,
        (HANDLE a, LPVOID b, DWORD c, LPVOID d), (a, b, c, d), FALSE)

// Set only inside eduke32.exe; every other host leaves the menu off and only forwards.
static bool s_active = false;

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);

    // Before any log line: a foreign host must not roll the game's ekmod.log either. A cut path fails closed.
    wchar_t exe[1024];
    const DWORD wn = GetModuleFileNameW(NULL, exe, 1024);
    if (wn == 0 || wn >= 1024 || !proxy_host_is_game(exe)) return TRUE;
    s_active = true;

    char host[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, host, MAX_PATH);

    ek_log("---- ekmod version.dll proxy attached ----");
    ek_log("host=%s", host);
    ek_log("proxy built " __DATE__ " " __TIME__);

    // Safe here: the loader snaps every IAT before any DllMain, so this is a pointer write into a finished table.
    iat_install_swap_hook();
  }
  // Unloading must leave the game's code intact: the firing hooks and camera bracket patch eduke32.exe to point here,
  // so a patch outliving the module jumps into unmapped memory. The SwapBuffers IAT entry (iat.cpp:102) has no removal;
  // harmless, as VERSION.dll is statically imported.
  else if (reason == DLL_PROCESS_DETACH && s_active) {
    gamehooks_remove();
    camera_remove();
    // Screenshot mode's call-site patch comes out too, before the cvars.
    shot_remove();
    // So does the world-quad layer's draw point (G_DrawRooms' renderDrawMasks call).
    maskscan_remove();
    // Last: a plain store into three engine cvars; a belt only, settings.cfg is already written on a clean quit
    // (shot.cpp).
    shot_restore();
  }
  return TRUE;
}
