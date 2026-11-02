#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "lua_api.h"
#include "game.h"
#include "log.h"

namespace fs = std::filesystem;

struct Handler {
    std::string event;
    std::string mod;
    int ref;
};

static lua_State* L;
static std::vector<Handler> g_handlers;
static std::string g_curMod;
static bool g_ready;

static std::mutex g_conMutex;
static std::deque<std::string> g_conQueue;
static bool g_conOpen;
static bool g_conStarted;
static std::mutex g_outMutex;
static std::string g_conLine;
static size_t g_conCursor;
static bool g_conPrompt;
static std::vector<std::string> g_conHistory;
static size_t g_conHistPos;
static std::string g_conDraft;

static void WriteRaw(const std::string& t)
{
    DWORD w = 0;
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), t.c_str(), (DWORD)t.size(), &w, nullptr);
}

static void DrawLocked(const std::string& output)
{
    std::string t = "\r\x1b[2K" + output + "> " + g_conLine;
    size_t back = g_conLine.size() - g_conCursor;
    if (back > 0) t += "\x1b[" + std::to_string(back) + "D";
    WriteRaw(t);
}

static void RedrawLocked()
{
    if (g_conPrompt) DrawLocked("");
}

static void ConWrite(const std::string& s)
{
    if (!g_conOpen) return;
    std::lock_guard<std::mutex> g(g_outMutex);
    if (g_conPrompt) DrawLocked(s);
    else WriteRaw(s);
}

static DWORD WINAPI ConsoleThread(LPVOID)
{
    if (!AllocConsole()) return 0;
    SetConsoleTitleW(L"SHIM console");
    HWND hwnd = GetConsoleWindow();
    if (hwnd) DeleteMenu(GetSystemMenu(hwnd, FALSE), SC_CLOSE, MF_BYCOMMAND);
    // icon aus der eingebauten resource, damit die konsole den shim-icon hat
    if (hwnd) {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&g_conOpen, &self);
        HICON icon = LoadIconW(self, MAKEINTRESOURCEW(1));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    }
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD outMode = 0;
    if (GetConsoleMode(out, &outMode)) SetConsoleMode(out, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    g_conOpen = true;
    // marker für den mod manager, nicht ändern
    ConWrite("Horsey Shim console. Type Lua, e.g. shim.get_money()\r\n");
    g_conPrompt = true;
    ConWrite("");

    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    for (;;) {
        INPUT_RECORD rec;
        DWORD n = 0;
        if (!ReadConsoleInputA(in, &rec, 1, &n) || n == 0) break;
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;
        char c = rec.Event.KeyEvent.uChar.AsciiChar;
        WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;

        std::string submitted;
        bool submit = false;
        {
            std::lock_guard<std::mutex> g(g_outMutex);
            std::string& line = g_conLine;
            if (c == '\r') {
                submitted = line;
                submit = true;
                if (!submitted.empty() && (g_conHistory.empty() || g_conHistory.back() != submitted))
                    g_conHistory.push_back(submitted);
                line.clear();
                g_conCursor = 0;
                g_conHistPos = g_conHistory.size();
                g_conDraft.clear();
                WriteRaw("\r\n");
                RedrawLocked();
            } else if (c == '\b') {
                if (g_conCursor > 0) {
                    line.erase(g_conCursor - 1, 1);
                    g_conCursor--;
                    RedrawLocked();
                }
            } else if (c >= 32 && c != 127) {
                line.insert(g_conCursor, 1, c);
                g_conCursor++;
                RedrawLocked();
            } else {
                switch (vk) {
                case VK_LEFT:
                    if (g_conCursor > 0) g_conCursor--;
                    RedrawLocked();
                    break;
                case VK_RIGHT:
                    if (g_conCursor < line.size()) g_conCursor++;
                    RedrawLocked();
                    break;
                case VK_HOME:
                    g_conCursor = 0;
                    RedrawLocked();
                    break;
                case VK_END:
                    g_conCursor = line.size();
                    RedrawLocked();
                    break;
                case VK_DELETE:
                    if (g_conCursor < line.size()) line.erase(g_conCursor, 1);
                    RedrawLocked();
                    break;
                case VK_UP:
                    if (g_conHistPos > 0) {
                        if (g_conHistPos == g_conHistory.size()) g_conDraft = line;
                        g_conHistPos--;
                        line = g_conHistory[g_conHistPos];
                        g_conCursor = line.size();
                        RedrawLocked();
                    }
                    break;
                case VK_DOWN:
                    if (g_conHistPos < g_conHistory.size()) {
                        g_conHistPos++;
                        line = (g_conHistPos == g_conHistory.size()) ? g_conDraft : g_conHistory[g_conHistPos];
                        g_conCursor = line.size();
                        RedrawLocked();
                    }
                    break;
                default:
                    break;
                }
            }
        }
        if (submit && !submitted.empty()) {
            std::lock_guard<std::mutex> g(g_conMutex);
            g_conQueue.push_back(submitted);
        }
    }
    return 0;
}

static void StartConsole(const fs::path& gameDir)
{
    if (g_conStarted) return;
    g_conStarted = true;
    std::error_code ec;
    if (fs::exists(gameDir / "shim_console.off", ec)) return;
    HANDLE t = CreateThread(nullptr, 0, ConsoleThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
}

static int TraceBack(lua_State* l)
{
    const char* msg = lua_tostring(l, 1);
    luaL_traceback(l, l, msg ? msg : "(non-string error)", 1);
    return 1;
}

static void Fire(const char* event, const std::function<int()>& pushArgs = nullptr)
{
    for (size_t i = 0; i < g_handlers.size();) {
        if (g_handlers[i].event != event) { i++; continue; }
        lua_pushcfunction(L, TraceBack);
        int tb = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, g_handlers[i].ref);
        int nargs = pushArgs ? pushArgs() : 0;
        g_curMod = g_handlers[i].mod;
        int rc = lua_pcall(L, nargs, 0, tb);
        g_curMod.clear();
        if (rc != LUA_OK) {
            Log("[%s] '%s' handler error, removed: %s", g_handlers[i].mod.c_str(), event, lua_tostring(L, -1));
            lua_pop(L, 1);
            luaL_unref(L, LUA_REGISTRYINDEX, g_handlers[i].ref);
            g_handlers.erase(g_handlers.begin() + i);
        } else {
            i++;
        }
        lua_settop(L, tb - 1);
    }
}

static int l_log(lua_State* l)
{
    int n = lua_gettop(l);
    std::string out;
    for (int i = 1; i <= n; i++) {
        size_t len;
        const char* s = luaL_tolstring(l, i, &len);
        if (i > 1) out += ' ';
        out.append(s, len);
        lua_pop(l, 1);
    }
    Log("[%s] %s", g_curMod.c_str(), out.c_str());
    ConWrite("[" + g_curMod + "] " + out + "\r\n");
    return 0;
}

static void ExecConsoleLine(const std::string& line)
{
    g_curMod = "console";
    int base = lua_gettop(L);
    lua_pushcfunction(L, TraceBack);
    int tb = lua_gettop(L);
    std::string expr = "return " + line;
    if (luaL_loadstring(L, expr.c_str()) != LUA_OK) {
        lua_pop(L, 1);
        if (luaL_loadstring(L, line.c_str()) != LUA_OK) {
            ConWrite(std::string(lua_tostring(L, -1)) + "\r\n");
            lua_settop(L, base);
            g_curMod.clear();
            return;
        }
    }
    if (lua_pcall(L, 0, LUA_MULTRET, tb) != LUA_OK) {
        ConWrite(std::string(lua_tostring(L, -1)) + "\r\n");
    } else {
        for (int i = tb + 1; i <= lua_gettop(L); i++) {
            size_t len;
            const char* s = luaL_tolstring(L, i, &len);
            ConWrite(std::string(s, len) + "\r\n");
            lua_pop(L, 1);
        }
    }
    lua_settop(L, base);
    g_curMod.clear();
}

static std::string QuoteLua(const std::string& s)
{
    std::string r = "\"";
    for (char c : s) {
        if (c == '\\') r += "\\\\";
        else if (c == '"') r += "\\\"";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else r += c;
    }
    return r + "\"";
}

static fs::path GameDir();

static int l_config(lua_State* l)
{
    luaL_checktype(l, 1, LUA_TTABLE);
    if (g_curMod.empty()) return luaL_error(l, "shim.config must be called while the mod is loading");
    fs::path file = GameDir() / "mods" / g_curMod / "config.lua";

    std::error_code ec;
    if (fs::exists(file, ec)) {
        lua_pushcfunction(l, TraceBack);
        if (luaL_loadfile(l, file.string().c_str()) != LUA_OK || lua_pcall(l, 0, 1, -2) != LUA_OK) {
            Log("[%s] config.lua error: %s", g_curMod.c_str(), lua_tostring(l, -1));
            lua_pop(l, 1);
        } else if (lua_istable(l, -1)) {
            lua_pushnil(l);
            while (lua_next(l, -2)) {
                int t = lua_type(l, -1);
                if (lua_type(l, -2) == LUA_TSTRING && (t == LUA_TNUMBER || t == LUA_TBOOLEAN || t == LUA_TSTRING)) {
                    lua_pushvalue(l, -2);
                    lua_pushvalue(l, -2);
                    lua_settable(l, 1);
                }
                lua_pop(l, 1);
            }
            lua_pop(l, 1);
        } else {
            Log("[%s] config.lua must return a table", g_curMod.c_str());
            lua_pop(l, 1);
        }
        lua_pop(l, 1);
    }

    std::vector<std::string> keys;
    lua_pushnil(l);
    while (lua_next(l, 1)) {
        int t = lua_type(l, -1);
        if (lua_type(l, -2) == LUA_TSTRING && (t == LUA_TNUMBER || t == LUA_TBOOLEAN || t == LUA_TSTRING))
            keys.push_back(lua_tostring(l, -2));
        lua_pop(l, 1);
    }
    std::sort(keys.begin(), keys.end());

    std::string out = "return {\n";
    for (auto& k : keys) {
        lua_getfield(l, 1, k.c_str());
        std::string v;
        if (lua_type(l, -1) == LUA_TBOOLEAN) v = lua_toboolean(l, -1) ? "true" : "false";
        else if (lua_type(l, -1) == LUA_TNUMBER) {
            if (lua_isinteger(l, -1)) v = std::to_string((long long)lua_tointeger(l, -1));
            else { char b[64]; snprintf(b, sizeof(b), "%.17g", (double)lua_tonumber(l, -1)); v = b; }
        } else v = QuoteLua(lua_tostring(l, -1));
        lua_pop(l, 1);
        out += "    " + k + " = " + v + ",\n";
    }
    out += "}\n";
    FILE* f = nullptr;
    if (fopen_s(&f, file.string().c_str(), "wb") == 0 && f) {
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
    } else {
        Log("[%s] could not write %s", g_curMod.c_str(), file.string().c_str());
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_on(lua_State* l)
{
    const char* event = luaL_checkstring(l, 1);
    luaL_checktype(l, 2, LUA_TFUNCTION);
    lua_pushvalue(l, 2);
    int ref = luaL_ref(l, LUA_REGISTRYINDEX);
    g_handlers.push_back({event, g_curMod, ref});
    return 0;
}

static void RequireWorld(lua_State* l)
{
    if (!GameReady()) luaL_error(l, "world not ready");
}

static int l_get_money(lua_State* l)
{
    RequireWorld(l);
    lua_pushinteger(l, GetMoney());
    return 1;
}

static int l_set_money(lua_State* l)
{
    RequireWorld(l);
    SetMoney((int)luaL_checkinteger(l, 1));
    return 0;
}

static int l_add_money(lua_State* l)
{
    RequireWorld(l);
    AddMoney((int)luaL_checkinteger(l, 1), lua_toboolean(l, 2) != 0);
    return 0;
}

static int l_spend_money(lua_State* l)
{
    RequireWorld(l);
    lua_pushboolean(l, SpendMoney((int)luaL_checkinteger(l, 1), lua_toboolean(l, 2) != 0));
    return 1;
}

static void PushHorse(lua_State* l, const HorseInfo& h)
{
    lua_createtable(l, 0, 11);
    lua_pushinteger(l, h.id);       lua_setfield(l, -2, "id");
    lua_pushstring(l, h.name);      lua_setfield(l, -2, "name");
    lua_pushinteger(l, h.state);    lua_setfield(l, -2, "state");
    lua_pushinteger(l, h.age);      lua_setfield(l, -2, "age");
    lua_pushinteger(l, h.maxAge);   lua_setfield(l, -2, "max_age");
    lua_pushboolean(l, h.hungry);   lua_setfield(l, -2, "hungry");
    lua_pushboolean(l, h.tired);    lua_setfield(l, -2, "tired");
    lua_pushboolean(l, h.old);      lua_setfield(l, -2, "old");
    lua_pushboolean(l, h.wild);     lua_setfield(l, -2, "wild");
    lua_pushnumber(l, h.x);         lua_setfield(l, -2, "x");
    lua_pushnumber(l, h.y);         lua_setfield(l, -2, "y");
}

static int l_horses(lua_State* l)
{
    RequireWorld(l);
    int n = HorseCount();
    lua_createtable(l, n, 0);
    int pos = 1;
    for (int i = 0; i < n; i++) {
        HorseInfo h;
        if (!GetHorse(i, &h)) continue;
        PushHorse(l, h);
        lua_rawseti(l, -2, pos++);
    }
    return 1;
}

static int l_set_horse(lua_State* l)
{
    RequireWorld(l);
    int i = (int)luaL_checkinteger(l, 1) - 1;
    const char* field = luaL_checkstring(l, 2);
    int value = lua_isboolean(l, 3) ? lua_toboolean(l, 3) : (int)luaL_checkinteger(l, 3);
    lua_pushboolean(l, SetHorseField(i, field, value));
    return 1;
}

static int l_frame(lua_State* l)
{
    lua_pushinteger(l, FrameCounter());
    return 1;
}

static int l_set_rest(lua_State* l)
{
    RequireWorld(l);
    int i = (int)luaL_checkinteger(l, 1) - 1;
    lua_pushboolean(l, SetHorseRest(i));
    return 1;
}

static int l_species(lua_State* l)
{
    const char* names[512];
    int n = EnumSpecies(names, 512);
    lua_createtable(l, n, 0);
    for (int i = 0; i < n; i++) {
        lua_pushstring(l, names[i]);
        lua_rawseti(l, -2, i + 1);
    }
    return 1;
}

static int l_world_size(lua_State* l)
{
    int w, h;
    if (!GetWorldSize(&w, &h)) return luaL_error(l, "world not ready");
    lua_pushinteger(l, w);
    lua_pushinteger(l, h);
    return 2;
}

static int l_spawn(lua_State* l)
{
    const char* species = luaL_checkstring(l, 1);
    int tx = (int)luaL_checkinteger(l, 2);
    int ty = (int)luaL_checkinteger(l, 3);
    int count = (int)luaL_optinteger(l, 4, 1);
    int radius = (int)luaL_optinteger(l, 5, 0);
    const char* herd = luaL_optstring(l, 6, species);
    int before = HorseCount();
    const char* err = SpawnHerd(species, tx, ty, count, radius, herd);
    int ww = 0, wh = 0;
    GetWorldSize(&ww, &wh);
    Log("[%s] spawn %s at tile %d,%d count %d radius %d: %s (scene %d, world %dx%d, horse actors %d -> %d)",
        g_curMod.c_str(), species, tx, ty, count, radius, err ? err : "ok",
        ActiveSceneId(), ww, wh, before, HorseCount());
    if (err) {
        lua_pushboolean(l, 0);
        lua_pushstring(l, err);
        return 2;
    }
    lua_pushboolean(l, 1);
    return 1;
}

static int l_spawn_recorded(lua_State* l)
{
    const char* species = luaL_checkstring(l, 1);
    int tx = (int)luaL_checkinteger(l, 2);
    int ty = (int)luaL_checkinteger(l, 3);
    const char* err = nullptr;
    int id = SpawnRecordedHorse(species, tx, ty, &err);
    Log("[%s] spawn_recorded %s at tile %d,%d: %s (record %d)", g_curMod.c_str(), species, tx, ty,
        id != -1 ? "ok" : err, id);
    if (id == -1) {
        lua_pushboolean(l, 0);
        lua_pushstring(l, err ? err : "failed");
        return 2;
    }
    lua_pushinteger(l, id);
    return 1;
}

static int l_clone(lua_State* l)
{
    int i = (int)luaL_checkinteger(l, 1) - 1;
    const char* err = nullptr;
    int id = CloneHorse(i, &err);
    if (id == -1) {
        lua_pushboolean(l, 0);
        lua_pushstring(l, err ? err : "failed");
        return 2;
    }
    lua_pushinteger(l, id);
    return 1;
}

static int l_clone_id(lua_State* l)
{
    int id = (int)luaL_checkinteger(l, 1);
    const char* err = nullptr;
    int newId = CloneHorseById(id, &err);
    if (newId == -1) {
        lua_pushboolean(l, 0);
        lua_pushstring(l, err ? err : "failed");
        return 2;
    }
    lua_pushinteger(l, newId);
    return 1;
}

static int l_compare(lua_State* l)
{
    int a = (int)luaL_checkinteger(l, 1) - 1;
    int b = (int)luaL_checkinteger(l, 2) - 1;
    const char* err = nullptr;
    std::string text = CompareGenes(a, b, &err);
    if (err) {
        lua_pushboolean(l, 0);
        lua_pushstring(l, err);
        return 2;
    }
    lua_pushlstring(l, text.data(), text.size());
    return 1;
}

static int l_set_name(lua_State* l)
{
    int id = (int)luaL_checkinteger(l, 1);
    const char* name = luaL_checkstring(l, 2);
    const char* err = nullptr;
    if (!SetRecordName(id, name, &err)) {
        lua_pushboolean(l, 0);
        lua_pushstring(l, err ? err : "failed");
        return 2;
    }
    lua_pushboolean(l, 1);
    return 1;
}

static int l_genes(lua_State* l)
{
    RequireWorld(l);
    int i = (int)luaL_checkinteger(l, 1) - 1;
    int n = GeneCount();
    std::vector<float> v((size_t)n);
    if (!GetHorseGenes(i, v.data(), n)) return luaL_error(l, "no horse at position %d", i + 1);
    lua_createtable(l, 0, n);
    for (int g = 0; g < n; g++) {
        lua_pushnumber(l, v[(size_t)g]);
        lua_setfield(l, -2, GeneName(g));
    }
    return 1;
}

static int l_active_scene(lua_State* l)
{
    lua_pushinteger(l, ActiveSceneId());
    return 1;
}

static bool g_reloadPending;

static int l_reload(lua_State* l)
{
    (void)l;
    g_reloadPending = true;
    return 0;
}

struct Timer {
    int id;
    std::string mod;
    int ref;
    long long due;
    int interval;
};

static std::vector<Timer> g_timers;
static int g_nextTimerId = 1;
static long long g_tickNo;

static int AddTimer(lua_State* l, bool repeat)
{
    lua_Integer ticks = luaL_checkinteger(l, 1);
    luaL_checktype(l, 2, LUA_TFUNCTION);
    if (ticks < 1) return luaL_error(l, "timer interval must be at least 1 tick");
    lua_pushvalue(l, 2);
    int ref = luaL_ref(l, LUA_REGISTRYINDEX);
    Timer t{g_nextTimerId++, g_curMod, ref, g_tickNo + ticks, repeat ? (int)ticks : 0};
    g_timers.push_back(t);
    lua_pushinteger(l, t.id);
    return 1;
}

static int l_after(lua_State* l) { return AddTimer(l, false); }
static int l_every(lua_State* l) { return AddTimer(l, true); }

static int l_cancel(lua_State* l)
{
    int id = (int)luaL_checkinteger(l, 1);
    for (size_t i = 0; i < g_timers.size(); i++) {
        if (g_timers[i].id != id) continue;
        luaL_unref(l, LUA_REGISTRYINDEX, g_timers[i].ref);
        g_timers.erase(g_timers.begin() + i);
        lua_pushboolean(l, 1);
        return 1;
    }
    lua_pushboolean(l, 0);
    return 1;
}

static std::vector<Timer>::iterator FindTimer(int id)
{
    return std::find_if(g_timers.begin(), g_timers.end(), [id](const Timer& t) { return t.id == id; });
}

static void ProcessTimers()
{
    g_tickNo++;
    std::vector<int> due;
    for (auto& t : g_timers)
        if (t.due <= g_tickNo) due.push_back(t.id);

    for (int id : due) {
        auto it = FindTimer(id);
        if (it == g_timers.end()) continue;
        std::string mod = it->mod;
        int ref = it->ref;

        lua_pushcfunction(L, TraceBack);
        int tb = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        g_curMod = mod;
        int rc = lua_pcall(L, 0, 0, tb);
        g_curMod.clear();
        if (rc != LUA_OK)
            Log("[%s] timer %d error, removed: %s", mod.c_str(), id, lua_tostring(L, -1));
        lua_settop(L, tb - 1);

        it = FindTimer(id);
        if (it == g_timers.end()) continue;
        if (rc != LUA_OK || it->interval == 0) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            g_timers.erase(it);
        } else {
            it->due = g_tickNo + it->interval;
        }
    }
}

static int l_location_name(lua_State* l);
static int l_record(lua_State* l);
static int l_spawn_recorded(lua_State* l);
static int l_clone(lua_State* l);
static int l_clone_id(lua_State* l);
static int l_compare(lua_State* l);
static int l_set_name(lua_State* l);

static int l_capture_horse(lua_State* l)
{
    RequireWorld(l);
    int i = (int)luaL_checkinteger(l, 1) - 1;
    const char* path = luaL_checkstring(l, 2);
    int size = (int)luaL_optinteger(l, 3, 256);
    float scale = (float)luaL_optnumber(l, 4, 1.0);
    float ox = (float)luaL_optnumber(l, 5, 0.0);
    float oy = (float)luaL_optnumber(l, 6, 0.0);
    bool local = lua_toboolean(l, 7) != 0;
    const char* err = nullptr;
    if (!CaptureHorse(i, path, size, scale, ox, oy, local, &err)) {
        lua_pushboolean(l, 0);
        lua_pushstring(l, err ? err : "failed");
        return 2;
    }
    lua_pushboolean(l, 1);
    return 1;
}

static int l_gene_defs(lua_State* l)
{
    int n = GeneCount();
    lua_createtable(l, n, 0);
    for (int i = 0; i < n; i++) {
        GeneDef d;
        if (!GetGeneDef(i, &d)) continue;
        lua_createtable(l, 0, 6);
        lua_pushinteger(l, i);
        lua_setfield(l, -2, "index");
        lua_pushstring(l, d.name.c_str());
        lua_setfield(l, -2, "name");
        lua_pushinteger(l, d.m);
        lua_setfield(l, -2, "m");
        lua_pushinteger(l, d.s);
        lua_setfield(l, -2, "s");
        lua_pushstring(l, d.letters.c_str());
        lua_setfield(l, -2, "letters");
        lua_createtable(l, 4, 0);
        for (int k = 0; k < 4; k++) {
            lua_pushnumber(l, d.value[k]);
            lua_rawseti(l, -2, k + 1);
        }
        lua_setfield(l, -2, "values");
        lua_rawseti(l, -2, i + 1);
    }
    return 1;
}

static int l_horse_genome(lua_State* l)
{
    RequireWorld(l);
    int i = (int)luaL_checkinteger(l, 1) - 1;
    uint8_t g[kGenomeBytes];
    if (!GetHorseGenome(i, g)) return luaL_error(l, "no horse at position %d", i + 1);
    static const char kHex[] = "0123456789abcdef";
    char out[kGenomeBytes * 2];
    for (int b = 0; b < kGenomeBytes; b++) {
        out[2 * b] = kHex[g[b] >> 4];
        out[2 * b + 1] = kHex[g[b] & 15];
    }
    lua_pushlstring(l, out, sizeof(out));
    return 1;
}

static int HexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int l_set_horse_genome(lua_State* l)
{
    RequireWorld(l);
    int i = (int)luaL_checkinteger(l, 1) - 1;
    size_t len = 0;
    const char* hex = luaL_checklstring(l, 2, &len);
    if (len != (size_t)kGenomeBytes * 2) {
        lua_pushnil(l);
        lua_pushstring(l, "genome must be 960 hex digits");
        return 2;
    }
    uint8_t g[kGenomeBytes];
    for (int b = 0; b < kGenomeBytes; b++) {
        int hi = HexDigit(hex[2 * b]), lo = HexDigit(hex[2 * b + 1]);
        if (hi < 0 || lo < 0) {
            lua_pushnil(l);
            lua_pushstring(l, "genome is not hex");
            return 2;
        }
        g[b] = (uint8_t)(hi << 4 | lo);
    }
    const char* err = nullptr;
    if (!SetHorseGenome(i, g, &err)) {
        lua_pushnil(l);
        lua_pushstring(l, err ? err : "failed");
        return 2;
    }
    lua_pushboolean(l, 1);
    return 1;
}

static int l_horse_actor(lua_State* l)
{
    RequireWorld(l);
    int i = (int)luaL_checkinteger(l, 1) - 1;
    lua_pushinteger(l, (lua_Integer)HorseActor(i));
    return 1;
}

static int l_horse_genome_actor(lua_State* l)
{
    RequireWorld(l);
    uintptr_t actor = (uintptr_t)luaL_checkinteger(l, 1);
    int id = (int)luaL_checkinteger(l, 2);
    uint8_t g[kGenomeBytes];
    if (!GetHorseGenomeByActor(actor, id, g)) {
        lua_pushnil(l);
        lua_pushstring(l, "that horse is no longer in the world, or changed");
        return 2;
    }
    static const char kHex[] = "0123456789abcdef";
    char out[kGenomeBytes * 2];
    for (int b = 0; b < kGenomeBytes; b++) {
        out[2 * b] = kHex[g[b] >> 4];
        out[2 * b + 1] = kHex[g[b] & 15];
    }
    lua_pushlstring(l, out, sizeof(out));
    return 1;
}

static int l_set_horse_genome_actor(lua_State* l)
{
    RequireWorld(l);
    uintptr_t actor = (uintptr_t)luaL_checkinteger(l, 1);
    int id = (int)luaL_checkinteger(l, 2);
    size_t len = 0;
    const char* hex = luaL_checklstring(l, 3, &len);
    if (len != (size_t)kGenomeBytes * 2) {
        lua_pushnil(l);
        lua_pushstring(l, "genome must be 960 hex digits");
        return 2;
    }
    uint8_t g[kGenomeBytes];
    for (int b = 0; b < kGenomeBytes; b++) {
        int hi = HexDigit(hex[2 * b]), lo = HexDigit(hex[2 * b + 1]);
        if (hi < 0 || lo < 0) {
            lua_pushnil(l);
            lua_pushstring(l, "genome is not hex");
            return 2;
        }
        g[b] = (uint8_t)(hi << 4 | lo);
    }
    const char* err = nullptr;
    if (!SetHorseGenomeByActor(actor, id, g, &err)) {
        lua_pushnil(l);
        lua_pushstring(l, err ? err : "failed");
        return 2;
    }
    lua_pushboolean(l, 1);
    return 1;
}

static const luaL_Reg kApi[] = {
    {"horse_actor", l_horse_actor},
    {"horse_genome_actor", l_horse_genome_actor},
    {"set_horse_genome_actor", l_set_horse_genome_actor},
    {"gene_defs", l_gene_defs},
    {"horse_genome", l_horse_genome},
    {"set_horse_genome", l_set_horse_genome},
    {"capture_horse", l_capture_horse},
    {"species", l_species},
    {"frame", l_frame},
    {"set_rest", l_set_rest},
    {"world_size", l_world_size},
    {"spawn", l_spawn},
    {"after", l_after},
    {"every", l_every},
    {"cancel", l_cancel},
    {"config", l_config},
    {"reload", l_reload},
    {"active_scene", l_active_scene},
    {"location_name", l_location_name},
    {"record", l_record},
    {"spawn_recorded", l_spawn_recorded},
    {"clone", l_clone},
    {"clone_id", l_clone_id},
    {"compare", l_compare},
    {"set_name", l_set_name},
    {"horses", l_horses},
    {"set_horse", l_set_horse},
    {"genes", l_genes},
    {"log", l_log},
    {"on", l_on},
    {"get_money", l_get_money},
    {"set_money", l_set_money},
    {"add_money", l_add_money},
    {"spend_money", l_spend_money},
    {nullptr, nullptr},
};

static fs::path GameDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

static constexpr long long kShimApiVersion = 1;

struct ModManifest {
    std::string id;
    long long apiVersion = 0;
};

static bool ReadTextFile(const fs::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

static bool IsTokenEnd(char c)
{
    return c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

struct JsonReader {
    const std::string& s;
    size_t i = 0;

    void Skip()
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    }

    bool Eat(char c)
    {
        Skip();
        if (i < s.size() && s[i] == c) {
            i++;
            return true;
        }
        return false;
    }

    bool String(std::string& out)
    {
        if (!Eat('"')) return false;
        out.clear();
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) return false;
            char e = s[i++];
            if (e == 'n') out += '\n';
            else if (e == 't') out += '\t';
            else if (e == 'u') {
                i += 4;
                out += '?';
            } else out += e;
        }
        return false;
    }

    bool SkipValue()
    {
        Skip();
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '"') {
            std::string ignored;
            return String(ignored);
        }
        if (c == '{') {
            i++;
            if (Eat('}')) return true;
            for (;;) {
                std::string key;
                if (!String(key) || !Eat(':') || !SkipValue()) return false;
                if (Eat(',')) continue;
                return Eat('}');
            }
        }
        if (c == '[') {
            i++;
            if (Eat(']')) return true;
            for (;;) {
                if (!SkipValue()) return false;
                if (Eat(',')) continue;
                return Eat(']');
            }
        }
        size_t start = i;
        while (i < s.size() && !IsTokenEnd(s[i])) i++;
        return i > start;
    }

    bool Int(long long& out)
    {
        Skip();
        size_t start = i;
        if (i < s.size() && s[i] == '-') i++;
        size_t digits = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
        if (i == digits || (i < s.size() && !IsTokenEnd(s[i]))) return false;
        out = strtoll(s.substr(start, i - start).c_str(), nullptr, 10);
        return true;
    }
};

static bool ParseManifest(const std::string& text, ModManifest& m)
{
    JsonReader r{text};
    if (!r.Eat('{')) return false;
    if (r.Eat('}')) return true;
    for (;;) {
        std::string key;
        if (!r.String(key) || !r.Eat(':')) return false;
        if (key == "id") {
            if (!r.String(m.id)) return false;
        } else if (key == "api_version") {
            if (!r.Int(m.apiVersion)) return false;
        } else if (!r.SkipValue()) {
            return false;
        }
        if (r.Eat(',')) continue;
        return r.Eat('}');
    }
}

static std::string CheckManifest(const fs::path& dir, const std::string& folder)
{
    std::string text;
    if (!ReadTextFile(dir / "manifest.json", text)) return "no manifest.json";
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    ModManifest m;
    if (!ParseManifest(text, m)) return "manifest.json is not valid";
    if (m.id != folder) return "manifest id '" + m.id + "' does not match the folder name";
    if (m.apiVersion < 1) return "manifest needs an api_version of 1 or higher";
    if (m.apiVersion > kShimApiVersion)
        return "needs api_version " + std::to_string(m.apiVersion) + ", this shim is " + std::to_string(kShimApiVersion);
    return "";
}

void LuaInit()
{
    L = luaL_newstate();
    luaL_openlibs(L);
    luaL_newlib(L, kApi);
    lua_setglobal(L, "shim");

    StartConsole(GameDir());

    fs::path modsDir = GameDir() / "mods";
    std::error_code ec;
    if (!fs::is_directory(modsDir, ec)) {
        Log("no mods folder at %s", modsDir.string().c_str());
        return;
    }

    Log("shim api version %lld", kShimApiVersion);
    for (auto& e : fs::directory_iterator(modsDir, ec)) {
        if (!e.is_directory()) continue;
        fs::path main = e.path() / "main.lua";
        if (!fs::exists(main, ec)) continue;
        std::string folder = e.path().filename().string();
        if (fs::exists(e.path() / "manifest.json.disabled", ec)) {
            Log("mod '%s' is disabled", folder.c_str());
            continue;
        }
        std::string reason = CheckManifest(e.path(), folder);
        if (!reason.empty()) {
            Log("mod '%s' skipped: %s", folder.c_str(), reason.c_str());
            continue;
        }

        g_curMod = folder;
        lua_pushcfunction(L, TraceBack);
        if (luaL_loadfile(L, main.string().c_str()) != LUA_OK || lua_pcall(L, 0, 0, -2) != LUA_OK) {
            Log("[%s] load error: %s", g_curMod.c_str(), lua_tostring(L, -1));
            lua_pop(L, 1);
        } else {
            Log("loaded mod '%s'", g_curMod.c_str());
        }
        lua_pop(L, 1);
    }
    g_curMod.clear();
}

struct KnownHorse {
    int id;
    std::string name;
    HorseInfo info;
};

static constexpr int kScanInterval = 30;
static constexpr int kMaxActors = 8192;
static constexpr size_t kBulkChange = 100;
static std::unordered_map<void*, KnownHorse> g_known;
static std::vector<void*> g_actorBuf;
static bool g_eventsSeeded;
static int g_scanCounter;

static void FireHorseEvent(const char* event, KnownHorse& k)
{
    k.info.name = k.name.c_str();
    Fire(event, [&]() { PushHorse(L, k.info); return 1; });
}

static void ScanHorseEvents()
{
    g_actorBuf.resize(kMaxActors);
    int n = EnumHorseActors(g_actorBuf.data(), kMaxActors);

    std::unordered_map<void*, bool> present;
    present.reserve((size_t)n);
    std::vector<void*> added;
    for (int i = 0; i < n; i++) {
        void* a = g_actorBuf[i];
        present[a] = true;
        if (g_known.find(a) != g_known.end()) continue;
        HorseInfo h;
        if (!GetHorseFromActor(a, &h)) continue;
        KnownHorse k;
        k.id = h.id;
        k.name = h.name ? h.name : "";
        k.info = h;
        g_known.emplace(a, std::move(k));
        added.push_back(a);
    }

    std::vector<KnownHorse> removed;
    for (auto it = g_known.begin(); it != g_known.end();) {
        if (present.find(it->first) == present.end()) {
            removed.push_back(std::move(it->second));
            it = g_known.erase(it);
        } else {
            ++it;
        }
    }

    if (!g_eventsSeeded) {
        g_eventsSeeded = true;
        return;
    }
    if (added.size() + removed.size() > kBulkChange) {
        Log("horse events: %zu added, %zu removed in one scan, treating as world reset", added.size(), removed.size());
        return;
    }
    for (auto& k : removed) FireHorseEvent("horse_removed", k);
    for (void* a : added) {
        auto it = g_known.find(a);
        if (it != g_known.end()) FireHorseEvent("horse_added", it->second);
    }
}

static int g_lastLocation = -2;

static void ScanLocation()
{
    int loc = ActiveSceneId();
    if (loc == g_lastLocation) return;
    int from = g_lastLocation;
    g_lastLocation = loc;
    if (from == -2) return;

    std::string fromName = LocationName(from);
    std::string toName = LocationName(loc);
    Log("location: %s (%d) -> %s (%d)", fromName.c_str(), from, toName.c_str(), loc);
    Fire("location_changed", [&]() {
        lua_pushstring(L, fromName.c_str());
        lua_pushstring(L, toName.c_str());
        lua_pushinteger(L, from);
        lua_pushinteger(L, loc);
        return 4;
    });
}

static int l_location_name(lua_State* l)
{
    int id = (int)luaL_checkinteger(l, 1);
    lua_pushstring(l, LocationName(id));
    return 1;
}

static int l_record(lua_State* l)
{
    int id = (int)luaL_checkinteger(l, 1);
    RecordInfo r;
    if (!GetRecord(id, &r)) {
        lua_pushnil(l);
        return 1;
    }
    lua_createtable(l, 0, 8);
    lua_pushinteger(l, r.id);          lua_setfield(l, -2, "id");
    lua_pushstring(l, r.name.c_str()); lua_setfield(l, -2, "name");
    lua_pushinteger(l, r.special);     lua_setfield(l, -2, "special");
    lua_pushinteger(l, r.colorA);      lua_setfield(l, -2, "color_a");
    lua_pushinteger(l, r.colorB);      lua_setfield(l, -2, "color_b");
    lua_pushinteger(l, r.parentA);     lua_setfield(l, -2, "parent_a");
    lua_pushinteger(l, r.parentB);     lua_setfield(l, -2, "parent_b");
    lua_createtable(l, (int)r.children.size(), 0);
    for (size_t i = 0; i < r.children.size(); i++) {
        lua_pushinteger(l, r.children[i]);
        lua_rawseti(l, -2, (lua_Integer)i + 1);
    }
    lua_setfield(l, -2, "children");
    return 1;
}

static std::unordered_map<std::string, std::optional<fs::file_time_type>> g_modTimes;
static bool g_modTimesSeeded;

static void CheckModChanges()
{
    std::error_code ec;
    fs::path gameDir = GameDir();
    if (fs::exists(gameDir / "shim_autoreload.off", ec)) return;

    bool changed = false;
    for (auto& e : fs::directory_iterator(gameDir / "mods", ec)) {
        if (!e.is_directory()) continue;
        for (const char* name : {"main.lua", "manifest.json", "manifest.json.disabled"}) {
            fs::path file = e.path() / name;
            std::optional<fs::file_time_type> now;
            auto t = fs::last_write_time(file, ec);
            if (!ec) now = t;
            ec.clear();
            auto it = g_modTimes.find(file.string());
            if (it == g_modTimes.end()) {
                g_modTimes[file.string()] = now;
                if (g_modTimesSeeded && now) changed = true;
            } else if (it->second != now) {
                it->second = now;
                changed = true;
            }
        }
    }
    g_modTimesSeeded = true;
    if (changed) {
        Log("mod files changed, reloading");
        g_reloadPending = true;
    }
}

void LuaTick()
{
    if (!L) return;
    std::deque<std::string> lines;
    {
        std::lock_guard<std::mutex> g(g_conMutex);
        lines.swap(g_conQueue);
    }
    for (auto& line : lines) ExecConsoleLine(line);

    if (!GameReady()) return;
    if (g_reloadPending) {
        g_reloadPending = false;
        lua_close(L);
        L = nullptr;
        g_handlers.clear();
        g_timers.clear();
        g_ready = false;
        LuaInit();
        Log("mods reloaded");
        return;
    }
    if (!g_ready) {
        g_ready = true;
        g_lastLocation = -2;
        LogWorldDebug();
        ScanHorseEvents();
        Fire("ready");
    }
    ScanLocation();
    if (++g_scanCounter % kScanInterval == 0) ScanHorseEvents();
    if (g_scanCounter % 65 == 0) CheckModChanges();
    ProcessTimers();
    Fire("tick");
}
