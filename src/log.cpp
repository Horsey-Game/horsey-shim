#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include "log.h"

static wchar_t g_logPath[MAX_PATH];

void LogInit(void* module)
{
    GetModuleFileNameW((HMODULE)module, g_logPath, MAX_PATH);
    wchar_t* slash = wcsrchr(g_logPath, L'\\');
    if (slash) *(slash + 1) = 0;
    wcscat_s(g_logPath, L"SHIM.log");
}

void Log(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 3, fmt, args);
    va_end(args);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 3) n = (int)sizeof(buf) - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    HANDLE f = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, buf, (DWORD)n, &written, nullptr);
    CloseHandle(f);
}
