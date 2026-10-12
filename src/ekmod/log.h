#pragma once
// No CRT, no allocation: runs during loader init.
void ek_log(const char* fmt, ...);
