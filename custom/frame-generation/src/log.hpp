#pragma once

#ifdef _WIN32
void LogInit(const wchar_t* path);
#else
void LogInit(const char* path);
#endif
void Log(const char* fmt, ...);
