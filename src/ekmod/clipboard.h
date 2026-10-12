#pragma once

// Windows clipboard, DLL-only (no decision to pin; campos.h holds the rules). Fail-closed: any failure answers false,
// changes nothing and never leaves the clipboard open. Window thread only.

// `out` is the empty string on every failure path; `cap` includes the terminator.
bool clipboard_read_text(char* out, int cap);

bool clipboard_write_text(const char* s);
