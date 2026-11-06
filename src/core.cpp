#include <windows.h>
#include "MinHook.h"
#include "core.h"
#include "log.h"
#include "scan.h"
#include "game.h"
#include "lua_api.h"

using GameFrameFn = void (*)();

static GameFrameFn g_gameFrame;
static unsigned long long g_frame;

static void HookedGameFrame()
{
    if (g_frame == 0) Log("first game_frame call");
    g_frame++;
    if (g_frame % 650 == 0) Log("game_frame count %llu", g_frame);
    g_gameFrame();
    LuaTick();
}

static constexpr DWORD kGameBuildStamp = 0x6A078A79;

DWORD WINAPI CoreThread(LPVOID)
{
    auto* base = (uint8_t*)GetModuleHandleW(nullptr);
    DWORD stamp = *(DWORD*)(base + *(int32_t*)(base + 0x3c) + 8);
    if (stamp != kGameBuildStamp) {
        Log("game build %08lX is not supported, shim stays off", stamp);
        return 0;
    }
    uintptr_t addr = FindPattern(
        "40 53 48 83 EC 40 0F 29 74 24 30 0F 29 7C 24 20 E8 ? ? ? ? 80 3D ? ? ? ? 00 0F 57 C0 F3 0F 2A 05 ? ? ? ?");
    if (!addr) {
        Log("game_frame signature not found");
        return 0;
    }
    Log("game_frame at %p (rva 0x%llx)", (void*)addr, (unsigned long long)(addr - (uintptr_t)GetModuleHandleW(nullptr)));

    if (MH_Initialize() != MH_OK) {
        Log("MH_Initialize failed");
        return 0;
    }
    if (MH_CreateHook((void*)addr, (void*)HookedGameFrame, (void**)&g_gameFrame) != MH_OK) {
        Log("MH_CreateHook failed");
        return 0;
    }
    if (!GameInit()) {
        Log("game does not match this shim, hooks not installed");
        return 0;
    }
    LuaInit();
    InstallBirthHooks();
    if (MH_EnableHook((void*)addr) != MH_OK) {
        Log("MH_EnableHook failed");
        return 0;
    }
    Log("game_frame hooked");
    return 0;
}
