// Reads EDuke32's own `bind "KEY" "FUNC"` lines from settings.cfg so a bind row can warn when the game already uses its
// key. Pure half; gamecfg.cpp does the file. Unlike World Tour, no key-name table is needed.
#pragma once
#include <string.h>

// The game's spelling of a virtual key (read from a real settings.cfg), or null. A key absent here never raises a
// warning: a missed warning is harmless, a wrong one misleads.
inline const char* ekm_gamecfg_key_name(int vk) {
  static const char kLetters[26][2] = {
    "A","B","C","D","E","F","G","H","I","J","K","L","M",
    "N","O","P","Q","R","S","T","U","V","W","X","Y","Z" };
  static const char kDigits[10][2] = {
    "0","1","2","3","4","5","6","7","8","9" };
  if (vk >= 0x41 && vk <= 0x5A) return kLetters[vk - 0x41];
  if (vk >= 0x30 && vk <= 0x39) return kDigits[vk - 0x30];
  if (vk >= 0x70 && vk <= 0x7B) {
    static const char* const kF[12] = { "F1","F2","F3","F4","F5","F6",
                                        "F7","F8","F9","F10","F11","F12" };
    return kF[vk - 0x70];
  }
  if (vk >= 0x60 && vk <= 0x69) {
    static const char* const kKp[10] = { "Kpad0","Kpad1","Kpad2","Kpad3","Kpad4",
                                         "Kpad5","Kpad6","Kpad7","Kpad8","Kpad9" };
    return kKp[vk - 0x60];
  }
  switch (vk) {
    case 0x08: return "BakSpc";   case 0x09: return "Tab";
    case 0x0D: return "Enter";    case 0x14: return "CapLck";
    case 0x1B: return "Escape";   case 0x20: return "Space";
    case 0x21: return "PgUp";     case 0x22: return "PgDn";
    case 0x23: return "End";      case 0x24: return "Home";
    case 0x25: return "Left";     case 0x26: return "Up";
    case 0x27: return "Right";    case 0x28: return "Down";
    case 0x2D: return "Insert";   case 0x2E: return "Delete";
    case 0x6A: return "Kpad*";    case 0x6B: return "Kpad+";
    case 0x6D: return "Kpad-";    case 0x6E: return "Kpad.";
    case 0x6F: return "Kpad/";    case 0x91: return "ScrLck";
    case 0xA0: return "LShift";   case 0xA1: return "RShift";
    case 0xA2: return "LCtrl";    case 0xA3: return "RCtrl";
    case 0xA4: return "LAlt";     case 0xA5: return "RAlt";
    case 0xBD: return "-";        case 0xBB: return "=";
    case 0xDB: return "[";        case 0xDD: return "]";
    case 0xBA: return ";";        case 0xDE: return "'";
    case 0xBF: return "/";        case 0xBC: return ",";
    case 0xBE: return ".";        case 0xDC: return "\\";
    default:   return 0;
  }
}

// KpdEnt is deliberately absent: keypad Enter arrives as VK_RETURN and bind.h drops the extended flag, so it is
// reported as "Enter" (a missed warning, never a wrong one).

// Case-insensitive: the file is inconsistent about case and a case-sensitive compare would silently disable rows.
inline int ekm_gamecfg_same_key(const char* a, const char* b) {
  if (!a || !b) return 0;
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return 0;
    a++; b++;
  }
  return (*a == 0 && *b == 0) ? 1 : 0;
}

// Returns 1 only for a complete `bind "KEY" "FUNC"`; truncated, unterminated or oversized input returns 0 so a partial
// parse never claims a binding.
inline int ekm_gamecfg_parse_line(const char* line, char* keyOut, int keyCap,
                                  char* fnOut, int fnCap) {
  if (!line || keyCap <= 0 || fnCap <= 0) return 0;
  const char* p = line;
  while (*p == ' ' || *p == '\t') p++;
  static const char kBind[] = "bind";
  int i = 0;
  for (; kBind[i]; i++) if (p[i] != kBind[i]) return 0;
  p += i;
  if (*p != ' ' && *p != '\t') return 0;
  while (*p == ' ' || *p == '\t') p++;

  if (*p != '"') return 0;
  p++;
  const char* keyStart = p;
  while (*p && *p != '"') p++;
  if (*p != '"') return 0;
  int keyLen = (int)(p - keyStart);
  if (keyLen <= 0 || keyLen >= keyCap) return 0;
  p++;

  while (*p == ' ' || *p == '\t') p++;
  if (*p != '"') return 0;
  p++;
  const char* fnStart = p;
  while (*p && *p != '"') p++;
  if (*p != '"') return 0;
  int fnLen = (int)(p - fnStart);
  if (fnLen <= 0 || fnLen >= fnCap) return 0;

  memcpy(keyOut, keyStart, (size_t)keyLen);
  keyOut[keyLen] = 0;
  memcpy(fnOut, fnStart, (size_t)fnLen);
  fnOut[fnLen] = 0;
  return 1;
}

// Guarded because test_host links the pure half only.
#ifndef EK_GAMECFG_PURE_ONLY
void gamecfg_load(void);
const char* gamecfg_function_for(int vk);
#endif
