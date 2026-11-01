#include <windows.h>
#include "names.h"
#include "log.h"
#include "core.h"

extern "C" void* g_real[17];
void* g_real[17];

static bool LoadReal()
{
    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\version.dll");
    HMODULE real = LoadLibraryW(path);
    if (!real) return false;
    for (int i = 0; i < 17; i++) {
        g_real[i] = (void*)GetProcAddress(real, kNames[i]);
        if (!g_real[i]) return false;
    }
    return true;
}

BOOL APIENTRY DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        LogInit(inst);
        if (!LoadReal()) {
            Log("failed to load real version.dll");
            return FALSE;
        }
        Log("proxy loaded");
        HANDLE t = CreateThread(nullptr, 0, CoreThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
