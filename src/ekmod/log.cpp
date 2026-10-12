#include <windows.h>
#include <stdio.h>
#include "log.h"

// One session per file: an appending log grew to 63 MB and old-build lines were misread as current evidence. Previous
// run kept as .prev. Interlocked: two racing first calls must roll once.
static void log_roll_once(const char* path) {
  static volatile LONG s_rolled = 0;
  if (InterlockedCompareExchange(&s_rolled, 1, 0) != 0) return;
  char prev[MAX_PATH];
  if (strlen(path) + 6 >= MAX_PATH) return;
  strcpy_s(prev, MAX_PATH, path);
  strcat_s(prev, MAX_PATH, ".prev");
  // Failure ignored: the roll is a convenience.
  MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);
}

void ek_log(const char* fmt, ...) {
  char path[MAX_PATH];
  const DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return;
  char* slash = strrchr(path, '\\');
  if (!slash) return;
  slash[1] = 0;
  if (strlen(path) + 12 >= MAX_PATH) return;
  strcat_s(path, MAX_PATH, "ekmod.log");
  log_roll_once(path);

  char line[1024];
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(line, sizeof(line) - 2, fmt, ap);
  va_end(ap);
  if (len <= 0) return;
  // Truncate, do not overrun: vsnprintf returns the length it WOULD have written.
  if (len > (int)sizeof(line) - 2) len = (int)sizeof(line) - 2;
  line[len] = '\n';

  // Open/close per line: no handle held open for runs that crash mid-frame.
  const HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(h, line, (DWORD)(len + 1), &written, NULL);
  CloseHandle(h);
}
