#include <windows.h>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include "MinHook.h"
#include <string>
#include <vector>
#include "game.h"
#include "log.h"
#include "scan.h"

using AddMoneyFn = void (*)(void* world, int amount, char silent);
using SpendMoneyFn = char (*)(void* world, int amount, char silent, char isPurchase);

static uint8_t** g_worldPtr;
static AddMoneyFn g_addMoney;
static SpendMoneyFn g_spendMoney;

static uint8_t** g_recordsPtr;

using SpawnHerdFn = void (*)(void* world, int cx, int cy, void* species, int count, int radius, void* herdName);
static SpawnHerdFn g_spawnHerd;
static uint8_t** g_popMapPtr;

static constexpr size_t kMoneyOffset = 0x308;
static constexpr size_t kActorsBegin = 0xb8;
static constexpr size_t kActorsEnd = 0xc0;
static constexpr size_t kActorX = 0x28;
static constexpr size_t kActorHorse = 0x80;
static constexpr size_t kRecordSize = 0x88;
static constexpr size_t kRecordNameLen = 0x10;
static constexpr size_t kRecordNameCap = 0x18;
static constexpr size_t kRecordHidden = 0x40;

bool GameInit()
{
    uintptr_t add = FindPattern(
        "48 83 EC 58 89 91 10 03 00 00 01 91 08 03 00 00 C7 81 0C 03 00 00 3C 00 00 00 45 84 C0 0F 85");
    uintptr_t spend = FindPattern(
        "40 53 48 83 EC 50 44 8B 91 08 03 00 00 44 3B D2 0F 8C ? ? ? ? 8B C2 F7 D8 89 81 10 03 00 00 44 2B D2 44 89 91 08 03 00 00 C7 81 0C 03 00 00");
    uintptr_t ref = FindPattern(
        "48 8B 0D ? ? ? ? 42 80 7C 07 09 00 74 12 45 33 C0 BA FA 00 00 00 E8");

    if (!add) Log("player_add_money signature not found");
    if (!spend) Log("spend_money signature not found");
    if (!ref) Log("world_obj signature not found");
    if (!add || !spend || !ref) return false;

    uintptr_t rec = FindPattern(
        "48 83 EC 28 83 F9 FF 75 18 48 8D 0D ? ? ? ? E8 ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? 48 63 C1 48 69 C0 88 00 00 00 48 03 05 ? ? ? ?");
    if (rec) {
        g_recordsPtr = (uint8_t**)(rec + 50 + *(int32_t*)(rec + 46));
        Log("horse records global at %p", (void*)g_recordsPtr);
    } else {
        Log("horse_record_get signature not found (horse names disabled)");
    }

    int32_t disp = *(int32_t*)(ref + 3);
    g_worldPtr = (uint8_t**)(ref + 7 + disp);
    g_addMoney = (AddMoneyFn)add;
    g_spendMoney = (SpendMoneyFn)spend;

    uintptr_t spawn = FindPattern(
        "48 89 5C 24 08 4C 89 4C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 30 49 8B F9 41 8B E8 8B DA 4C 8B F9 48 8B B4 24 A0 00 00 00 44 8B 94 24 90 00 00 00 45 85 D2 0F 8E");
    uintptr_t pop = FindPattern(
        "48 89 5C 24 18 56 57 41 57 48 81 EC 80 00 00 00 48 89 AC 24 A0 00 00 00 48 8B F2 48 8B 2D ? ? ? ? 4C 8B F9 48 8B DD 48 8B 7D 08 80 7F 19 00");
    if (spawn && pop) {
        g_popMapPtr = (uint8_t**)(pop + 34 + *(int32_t*)(pop + 30));
        g_spawnHerd = (SpawnHerdFn)spawn;
        Log("world_spawn_herd at %p, population map global at %p", (void*)spawn, (void*)g_popMapPtr);
    } else {
        Log("spawn signatures not found (spawning disabled)");
    }

    Log("game api ready: world_obj global at %p", (void*)g_worldPtr);
    return true;
}

bool GameReady()
{
    return g_worldPtr && *g_worldPtr;
}

int GetMoney()
{
    return *(int*)(*g_worldPtr + kMoneyOffset);
}

void SetMoney(int value)
{
    *(int*)(*g_worldPtr + kMoneyOffset) = value;
}

void AddMoney(int amount, bool silent)
{
    g_addMoney(*g_worldPtr, amount, silent ? 1 : 0);
}

bool SpendMoney(int amount, bool silent)
{
    return g_spendMoney(*g_worldPtr, amount, silent ? 1 : 0, 0) != 0;
}

static constexpr uintptr_t kVtHorseActor = 0x30ae08;
static constexpr uintptr_t kVtWildHorse = 0x310c90;

static bool ValidHorseActor(uint8_t* actor)
{
    __try {
        if (!actor) return false;
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        uintptr_t vt = *(uintptr_t*)actor;
        if (vt != base + kVtHorseActor && vt != base + kVtWildHorse) return false;
        return *(uint8_t**)(actor + kActorHorse) != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static uint8_t* NthHorseActor(int index)
{
    if (!GameReady() || index < 0) return nullptr;
    __try {
        uint8_t** begin = *(uint8_t***)(*g_worldPtr + kActorsBegin);
        uint8_t** end = *(uint8_t***)(*g_worldPtr + kActorsEnd);
        if (!begin || end < begin) return nullptr;
        int seen = 0;
        for (uint8_t** p = begin; p != end; ++p) {
            if (!ValidHorseActor(*p)) continue;
            if (seen++ == index) return *p;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

static constexpr uintptr_t kRvaRecordCreate = 0xc7ac0;
static constexpr size_t kHorseRecordId = 0x1f8;
using RecordCreateFn = int (*)(char flag, int colorA, int colorB, int parentA, int parentB);

static void CopyWorldActors(std::vector<uint8_t*>* out)
{
    out->clear();
    if (!GameReady()) return;
    __try {
        uint8_t** begin = *(uint8_t***)(*g_worldPtr + kActorsBegin);
        uint8_t** end = *(uint8_t***)(*g_worldPtr + kActorsEnd);
        if (!begin || end < begin) return;
        out->assign(begin, end);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out->clear();
    }
}

static int AssignRecord(uint8_t* actor, RecordCreateFn create)
{
    __try {
        uint8_t* horse = *(uint8_t**)(actor + kActorHorse);
        if (!horse || *(int*)(horse + kHorseRecordId) != -1) return -1;
        int id = create(0, 0, -1, -1, -1);
        *(int*)(horse + kHorseRecordId) = id;
        return id;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int SpawnRecordedHorse(const char* species, int tx, int ty, const char** err, void** horseOut)
{
    *err = nullptr;
    if (horseOut) *horseOut = nullptr;
    std::vector<uint8_t*> before, after;
    CopyWorldActors(&before);
    *err = SpawnHerd(species, tx, ty, 1, 0, species);
    if (*err) return -1;
    CopyWorldActors(&after);

    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    RecordCreateFn create = (RecordCreateFn)(base + kRvaRecordCreate);
    for (uint8_t* actor : after) {
        if (std::find(before.begin(), before.end(), actor) != before.end()) continue;
        if (!ValidHorseActor(actor)) continue;
        int id = AssignRecord(actor, create);
        if (id != -1) {
            if (horseOut) *horseOut = *(uint8_t**)(actor + kActorHorse);
            Log("spawned %s with record %d at tile %d,%d", species, id, tx, ty);
            return id;
        }
    }
    *err = "no new horse was found to record";
    return -1;
}

static constexpr uintptr_t kRvaHorseSetGenes = 0xb3690;
static constexpr uintptr_t kRvaHorseApplyGenes = 0xb3110;
static constexpr size_t kGenomeOffset = 0x2b8;
static constexpr float kTileSize = 32.0f;
using HorseSetGenesFn = char (*)(uint8_t* horse, uint8_t* genes);
using HorseApplyGenesFn = void (*)(uint8_t* horse);

static bool ReadActorTile(uint8_t* actor, int* tx, int* ty, uint8_t** horse)
{
    __try {
        float x = *(float*)(actor + kActorX);
        float y = *(float*)(actor + kActorX + 4);
        *tx = (int)floorf(x / kTileSize);
        *ty = (int)floorf(y / kTileSize);
        *horse = *(uint8_t**)(actor + kActorHorse);
        return *horse != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool CopyGenome(uint8_t* from, uint8_t* to)
{
    __try {
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        HorseSetGenesFn setGenes = (HorseSetGenesFn)(base + kRvaHorseSetGenes);
        HorseApplyGenesFn applyGenes = (HorseApplyGenesFn)(base + kRvaHorseApplyGenes);
        *(int*)(to + 0x1fc) = *(int*)(from + 0x1fc);
        *(int*)(to + 0x200) = *(int*)(from + 0x200);
        setGenes(to, from + kGenomeOffset);
        applyGenes(to);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int CloneFromActor(uint8_t* actor, const char** err)
{
    *err = nullptr;
    int tx = 0, ty = 0;
    uint8_t* source = nullptr;
    if (!ReadActorTile(actor, &tx, &ty, &source)) {
        *err = "could not read that horse";
        return -1;
    }
    void* clone = nullptr;
    int id = SpawnRecordedHorse("default", tx, ty, err, &clone);
    if (id == -1) return -1;
    if (!CopyGenome(source, (uint8_t*)clone)) {
        *err = "copying the genome failed";
        return -1;
    }
    Log("cloned a horse into record %d at tile %d,%d", id, tx, ty);
    return id;
}

int CloneHorse(int index, const char** err)
{
    *err = nullptr;
    uint8_t* actor = NthHorseActor(index);
    if (!actor) {
        *err = "no horse at that position";
        return -1;
    }
    return CloneFromActor(actor, err);
}

static bool ActorHasRecord(uint8_t* actor, int id)
{
    __try {
        uint8_t* horse = *(uint8_t**)(actor + kActorHorse);
        return horse && *(int*)(horse + kHorseRecordId) == id;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int CloneHorseById(int recordId, const char** err)
{
    *err = nullptr;
    if (recordId < 0) {
        *err = "no horse with that id";
        return -1;
    }
    std::vector<uint8_t*> actors;
    CopyWorldActors(&actors);
    for (uint8_t* actor : actors) {
        if (!ValidHorseActor(actor) || !ActorHasRecord(actor, recordId)) continue;
        return CloneFromActor(actor, err);
    }
    *err = "no horse with that id is on the map (it may be in a building)";
    return -1;
}

int HorseCount()
{
    if (!GameReady()) return 0;
    int n = 0;
    __try {
        uint8_t** begin = *(uint8_t***)(*g_worldPtr + kActorsBegin);
        uint8_t** end = *(uint8_t***)(*g_worldPtr + kActorsEnd);
        if (!begin || end < begin) return 0;
        for (uint8_t** p = begin; p != end; ++p)
            if (ValidHorseActor(*p)) n++;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return n;
}

static bool GetHorseImpl(uint8_t* actor, HorseInfo* out)
{
    uint8_t* horse = *(uint8_t**)(actor + kActorHorse);

    out->id = *(int*)(horse + 0x1f8);
    out->state = *(int*)(horse + 0x1c);
    out->age = *(int*)(horse + 0x1fc);
    out->maxAge = *(int*)(horse + 0x200);
    out->hungry = *(char*)(horse + 0x205) != 0;
    out->tired = *(char*)(horse + 0x206) != 0;
    out->old = out->maxAge <= out->age;
    out->wild = *(uintptr_t*)actor == (uintptr_t)GetModuleHandleW(nullptr) + kVtWildHorse;
    out->x = *(float*)(actor + kActorX);
    out->y = *(float*)(actor + kActorX + 4);

    out->name = "";
    if (g_recordsPtr && *g_recordsPtr && out->id != -1) {
        uint8_t* rec = *g_recordsPtr + (size_t)out->id * kRecordSize;
        if (*(char*)(rec + kRecordHidden) == 0 && *(size_t*)(rec + kRecordNameLen) != 0)
            out->name = *(size_t*)(rec + kRecordNameCap) > 0xf ? *(const char**)rec : (const char*)rec;
    }
    return true;
}

bool GetHorse(int index, HorseInfo* out)
{
    uint8_t* actor = NthHorseActor(index);
    if (!actor) return false;
    __try {
        return GetHorseImpl(actor, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int EnumHorseActors(void** out, int max)
{
    if (!GameReady()) return 0;
    int n = 0;
    __try {
        uint8_t** begin = *(uint8_t***)(*g_worldPtr + kActorsBegin);
        uint8_t** end = *(uint8_t***)(*g_worldPtr + kActorsEnd);
        if (!begin || end < begin) return 0;
        for (uint8_t** p = begin; p != end && n < max; ++p)
            if (ValidHorseActor(*p)) out[n++] = *p;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return n;
}

bool GetHorseFromActor(void* actor, HorseInfo* out)
{
    if (!actor) return false;
    __try {
        return GetHorseImpl((uint8_t*)actor, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetHorseField(int index, const char* field, int value)
{
    uint8_t* actor = NthHorseActor(index);
    if (!actor) return false;
    __try {
        uint8_t* horse = *(uint8_t**)(actor + kActorHorse);
        if (!strcmp(field, "hungry")) *(char*)(horse + 0x205) = value != 0;
        else if (!strcmp(field, "tired")) *(char*)(horse + 0x206) = value != 0;
        else if (!strcmp(field, "age")) *(int*)(horse + 0x1fc) = value;
        else return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// frame counter, fester offset für diesen build
static constexpr uintptr_t kRvaFrameCounter = 0x3ee950;
static constexpr uintptr_t kRvaHorseSetNeedsRest = 0xb92c0;
using HorseSetNeedsRestFn = void (*)(uint8_t* horse);

int FrameCounter()
{
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    return *(int*)(base + kRvaFrameCounter);
}

bool SetHorseRest(int index)
{
    uint8_t* actor = NthHorseActor(index);
    if (!actor) return false;
    __try {
        uint8_t* horse = *(uint8_t**)(actor + kActorHorse);
        if (!horse) return false;
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        // state 6 ignoriert das spiel, also nichts machen
        if (*(int*)(horse + 0x1c) == 6) return false;
        HorseSetNeedsRestFn setRest = (HorseSetNeedsRestFn)(base + kRvaHorseSetNeedsRest);
        setRest(horse);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int ActiveSceneId()
{
    if (!GameReady()) return -1;
    return *(int*)(*g_worldPtr + 0x25C);
}

const char* LocationName(int index)
{
    static std::string name;
    name.clear();
    if (index == -1) {
        name = "map";
        return name.c_str();
    }
    if (!GameReady() || index < 0 || index > 255) return name.c_str();
    __try {
        uint8_t* table = *(uint8_t**)(*g_worldPtr + 0x438);
        uint8_t* loc = table ? *(uint8_t**)(table + (size_t)index * 8) : nullptr;
        if (loc) {
            size_t size = *(size_t*)(loc + 0x48);
            size_t cap = *(size_t*)(loc + 0x50);
            const char* p = cap > 0xf ? *(const char**)(loc + 0x38) : (const char*)(loc + 0x38);
            if (p && size < 256) name.assign(p, size);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        name.clear();
    }
    return name.c_str();
}

bool GetRecord(int id, RecordInfo* out)
{
    if (!g_recordsPtr || !*g_recordsPtr || id < 0) return false;
    __try {
        uint8_t* begin = *g_recordsPtr;
        uint8_t* end = *(uint8_t**)((uint8_t*)g_recordsPtr + 8);
        if (!begin || end < begin) return false;
        size_t count = (size_t)(end - begin) / kRecordSize;
        if ((size_t)id >= count) return false;

        uint8_t* rec = begin + (size_t)id * kRecordSize;
        out->id = id;
        out->name.clear();
        size_t nlen = *(size_t*)(rec + kRecordNameLen);
        if (nlen > 0 && nlen < 256) {
            size_t cap = *(size_t*)(rec + kRecordNameCap);
            const char* p = cap > 0xf ? *(const char**)rec : (const char*)rec;
            out->name.assign(p, nlen);
        }
        out->special = *(char*)(rec + kRecordHidden);
        out->colorA = *(int*)(rec + 0x44);
        out->colorB = *(int*)(rec + 0x64);
        out->parentA = *(int*)(rec + 0x48);
        out->parentB = *(int*)(rec + 0x4c);

        out->children.clear();
        uint8_t* cb = *(uint8_t**)(rec + 0x70);
        uint8_t* ce = *(uint8_t**)(rec + 0x78);
        if (cb && ce >= cb && (ce - cb) / 4 < 1000)
            for (uint8_t* p = cb; p < ce; p += 4) out->children.push_back(*(int*)p);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static constexpr uintptr_t kRvaHorseInitFromParents = 0xb2ed0;
static constexpr uintptr_t kRvaHerdInterbreed = 0x816f0;
using InitFromParentsFn = void (*)(uintptr_t child, uintptr_t parentA, uintptr_t parentB);
using HerdInterbreedFn = void (*)(uintptr_t actorA, uintptr_t actorB);

static InitFromParentsFn g_initFromParentsOrig;
static HerdInterbreedFn g_herdInterbreedOrig;
static RecordCreateFn g_recordCreate;
static int g_herdDepth;

static bool RecordIsLive(int id)
{
    if (id < 0 || !g_recordsPtr || !*g_recordsPtr) return false;
    uint8_t* begin = *g_recordsPtr;
    uint8_t* end = *(uint8_t**)((uint8_t*)g_recordsPtr + 8);
    if (!begin || end < begin || (size_t)id >= (size_t)(end - begin) / kRecordSize) return false;
    return *(uint64_t*)(begin + (size_t)id * kRecordSize + 0x10) != 0;
}

static void EnsureRecord(uintptr_t horse)
{
    __try {
        int id = *(int*)(horse + kHorseRecordId);
        if (RecordIsLive(id)) return;
        if (id == -1) {
            id = g_recordCreate(0, 0, -1, -1, -1);
            *(int*)(horse + kHorseRecordId) = id;
        }
        char name[16];
        snprintf(name, sizeof(name), "wild %d", id);
        const char* err = nullptr;
        if (SetRecordName(id, name, &err)) Log("wild horse recorded as %d", id);
        else Log("wild horse %d not named: %s", id, err ? err : "unknown");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void HookedHorseInitFromParents(uintptr_t child, uintptr_t parentA, uintptr_t parentB)
{
    g_initFromParentsOrig(child, parentA, parentB);
    if (g_herdDepth == 0) return;
    EnsureRecord(parentA);
    EnsureRecord(parentB);
}

static void HookedHerdInterbreed(uintptr_t actorA, uintptr_t actorB)
{
    g_herdDepth++;
    __try {
        g_herdInterbreedOrig(actorA, actorB);
    } __finally {
        g_herdDepth--;
    }
}

void InstallBirthHooks()
{
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    g_recordCreate = (RecordCreateFn)(base + kRvaRecordCreate);
    void* initAddr = (void*)(base + kRvaHorseInitFromParents);
    void* herdAddr = (void*)(base + kRvaHerdInterbreed);
    if (MH_CreateHook(initAddr, (void*)HookedHorseInitFromParents, (void**)&g_initFromParentsOrig) != MH_OK ||
        MH_CreateHook(herdAddr, (void*)HookedHerdInterbreed, (void**)&g_herdInterbreedOrig) != MH_OK) {
        Log("birth hooks: MH_CreateHook failed");
        return;
    }
    if (MH_EnableHook(initAddr) != MH_OK || MH_EnableHook(herdAddr) != MH_OK) {
        Log("birth hooks: MH_EnableHook failed");
        return;
    }
    Log("birth hooks installed");
}

struct MsvcString {
    char buf[16];
    size_t size;
    size_t cap;
};
static constexpr uintptr_t kRvaRecordSetName = 0xc7dd0;
using RecordSetNameFn = void (*)(int recordIdx, MsvcString* name);

static bool CallRecordSetName(int recordId, MsvcString* name)
{
    __try {
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        RecordSetNameFn setName = (RecordSetNameFn)(base + kRvaRecordSetName);
        setName(recordId, name);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetRecordName(int recordId, const char* name, const char** err)
{
    *err = nullptr;
    size_t len = strlen(name);
    if (len == 0 || len > 15) {
        *err = "name must be 1 to 15 characters";
        return false;
    }
    RecordInfo r;
    if (!GetRecord(recordId, &r)) {
        *err = "no horse record with that id";
        return false;
    }
    MsvcString s = {};
    memcpy(s.buf, name, len);
    s.buf[len] = 0;
    s.size = len;
    s.cap = 15;
    if (!CallRecordSetName(recordId, &s)) {
        *err = "naming failed";
        return false;
    }
    Log("record %d named %s", recordId, name);
    return true;
}

std::string CompareGenes(int a, int b, const char** err)
{
    *err = nullptr;
    int n = GeneCount();
    std::vector<float> va((size_t)n), vb((size_t)n);
    if (!GetHorseGenes(a, va.data(), n)) {
        *err = "no horse at the first position";
        return "";
    }
    if (!GetHorseGenes(b, vb.data(), n)) {
        *err = "no horse at the second position";
        return "";
    }
    std::string lines;
    int diffs = 0;
    for (int g = 0; g < n; g++) {
        if (va[(size_t)g] == vb[(size_t)g]) continue;
        diffs++;
        char line[128];
        snprintf(line, sizeof(line), "%-16s %g -> %g\r\n", GeneName(g), va[(size_t)g], vb[(size_t)g]);
        lines += line;
    }
    char head[64];
    snprintf(head, sizeof(head), "%d genes differ\r\n", diffs);
    return std::string(head) + lines;
}

void LogWorldDebug()
{
    if (!GameReady()) return;
    __try {
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        uint8_t** begin = *(uint8_t***)(*g_worldPtr + kActorsBegin);
        uint8_t** end = *(uint8_t***)(*g_worldPtr + kActorsEnd);
        Log("world list: %lld entries, %d horse actors", (long long)(end - begin), HorseCount());
        int shown = 0;
        for (uint8_t** p = begin; p != end && shown < 8; ++p, ++shown) {
            uintptr_t vt = *(uintptr_t*)*p;
            Log("  entry %d: vtable rva 0x%llx", shown, (unsigned long long)(vt - base));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("world list debug failed (bad pointer)");
    }
}
static constexpr size_t kHorseGenome = 0x2b8;
static constexpr uintptr_t kRvaGeneGetFloat = 0xa5dc0;
static constexpr int kGeneCount = 240;
using GeneGetFloatFn = float (*)(void* genome, int index);

static const char kGeneNames[] =
    "SIZE,ASPECT,SKINNY,BONES,BONES2,CHEST_BIG,CHEST_SMALL,GIANT_DWARF,MUSCLE_USE,QUADRUPED,"
    "BIPED,SPLAY,LEG_IN,LEG_IN2,GUT,GUT_IS_UDDER,OSTODERM,OSTO_SIZE,DERRIERE,SPEED_FACTOR,"
    "BREAK_FORCE,TAIL_TAG,TAIL_EXISTS,TAIL_SIZE,TAIL_SHORT,TAIL_ASPECT,TAIL_ANGLE,"
    "TAIL_JOINT_TYPE,TAIL_STIFF,TAIL_SPEED,TAIL_FLEXIBILITY,TAIL_SHAPE,TAIL_BOTTOM,"
    "TAIL_SEGMENTS,LEG_TAG,LEG_TYPE,LEG_LENGTH,LEG_STRETCH,LEG_STRETCH2,LEG_STRENGTH,"
    "LEG_HAS_FOOT,LEG_JOINT_TYPE,LEG_FLEXIBILITY,LEG_FLEX_BIAS,LEG_THRUST_BACK,LEG_IS_CIRCLE,"
    "LEG_COUNT,LEG_SKEW,LEG_PENCIL,LEG_AND_ARM_LIMP,HAS_KNEE,KNEE_MIN,KNEE_MAX,ARM_TAG,"
    "ARM_TYPE,ARM_LENGTH,ARM_STRETCH,ARM_STRETCH2,ARM_STRENGTH,ARM_HAS_HAND,ARM_JOINT_TYPE,"
    "ARM_FLEXIBILITY,ARM_FLEX_BIAS,ARM_FORWARD,ARM_SKEW,ARM_NODE_SCALE,HAS_ELBOW,ELBOW_RANGE,"
    "UPARM_TAG,UPARM_Y,UPARM_ANGLE,UPARM_GOOFY,NECK_TAG,NECK_TYPE,NECK_LENGTH,NECK_GIRAFFE,"
    "NECK_THICKNESS,NECK_ANGLE,NECK_COCK,NECK_JOINT_TYPE,NECK_FLEXIBILITY,NECK_FLEX_BIAS,"
    "NECK_SLOUCH,NECK_ONTOP,NECK_STIFF,NECK_SPEED,HAS_FOOT,FOOT_SIZE,FOOT_CLOWN,FOOT_THICKNESS,"
    "FOOT_TOE,FOOT_IS_CIRCLE,FOOT_BACKWARDS,HAS_HAND,HAND_WIDTH,HAND_LENGTH,HAND_FINGER,"
    "HEAD_SIZE,HEAD_THICK_SKULL,HEAD_X_GROWTH,HEAD_Y_GROWTH,HEAD_ASPECT,HEAD_SQUARE,"
    "HEAD_HAS_BACK,HEAD_GIANT,HEAD_SHRUNK,HEAD_JOINTED,HEAD_CHIMERA,EYE_STYLE,BUGEYE,EYEBOX_X,"
    "EYEBOX_Y,EYEBOX_SIZE,EYE_SIZE,PUPIL_SIZE,HAS_PUPIL,BROW_SIZE,BROW_SLANT,EAR_STYLE,"
    "EAR_SHAPE,EAR_FLOP,EAR_X,EAR_SIZE,EAR_ASPECT,EAR_SLANT,EAR_INTERIOR,TEETH_SHAPE,HAS_MOUTH,"
    "MOUTH_Y,MOUTH_SIZE,JAW,TEETH_UPPER,TEETH_UPPER2,TONGUE,TONGUE_SEGS,NOSE_STYLE,NOSE_INNY,"
    "NOSE_Y,NOSE_SIZE,NOSE_INTERIOR,HAS_ANTLERS,ANTLER_X,ANTLER_W,ANTLER_H,ANTLER_TAPER,"
    "ANTLER_POM,ANTLER_COLOR,POM_COLOR,POM_USECOLOR,ANTLER_REC,ANTLER_REC2,ANTLER_FLIP,"
    "ANTLER_MOD,ANTLER_SCALEH,ANTLER_SCALEW,ANTLER_ANGLE,ANTLER_ANGLE2,ANTLER_ANGLE_RAND,"
    "ANTLER_T1,ANTLER_T2,HAT_EXISTS,HAT_SIZE,HAT_RAKE,HAT_ASPECT,HAT_TAPER,HAT_POM,"
    "HAT_POM_IS_LID,HAT_CLONE,HAT_BACK_SCALE,HAT_FRONT_SCALE,HAT_BACK_ANGLE,HAT_FRONT_ANGLE,"
    "HAT_ANGLE_RAND,HAT_FLIP,HAT_T,BASE_BROWN,BASE_BLACK,BASE_RED,BASE_GREEN,GREEN_KNOCKOUT,"
    "BASE_CREAM,ALT_BLUE,SPOT_YELLOW,SKIN_HUE,SKIN_HUE2,SWAP_BASE_SPOT,SWAP_ALT_SPOT,WHITE,"
    "WHITE_IS_LETHAL,EYE_HUE,NOSE_HUE,HOOF_COLOR,AGOUTI,BELLY_ALT,SKIN_HEAD,SKIN_HANDS,"
    "FOOT_IS_HOOF,RACCOON_EYE,EAR_COMP,TAIL_ALT,PAT_SPLIT,PAT_BELLY,PAT_STRIPE,PAT_SPOT,"
    "PAT_PERLIN,PAT_PERLIN2,PAT_PERLIN_SIZE,RAMPAGE,SPINAL_LOCO,BRAIN_SPASTIC,HIGH_INTELLECT,"
    "OMNIVORE,LITTER_SIZE,OLD_AGE,LIMP,NARCOLEPSY,FLU_IMMUNITY,TAIL_WAG,STIFF_JOINTS,"
    "L_LEG_SIGNAL,L_LEG_FTOB_REACT,L_LEG_FTOB_EVENT,L_LEG_BTOF_REACT,L_LEG_BTOF_EVENT,"
    "L_ARM_SIGNAL,L_ARM_FTOB_REACT,L_ARM_FTOB_EVENT,L_ARM_BTOF_REACT,L_ARM_BTOF_EVENT,"
    "L_TAIL_SIGNAL,L_TAIL_FTOB_REACT,L_TAIL_FTOB_EVENT,L_TAIL_BTOF_REACT,L_TAIL_BTOF_EVENT,"
    "L_NECK_SIGNAL,L_NECK_FTOB_REACT,L_NECK_FTOB_EVENT,L_NECK_BTOF_REACT,L_NECK_BTOF_EVENT,"
    "LOCO_SYNC";

static const std::vector<std::string>& GeneNameList()
{
    static std::vector<std::string> names;
    if (names.empty()) {
        const char* p = kGeneNames;
        for (;;) {
            const char* e = strchr(p, ',');
            names.emplace_back(p, e ? (size_t)(e - p) : strlen(p));
            if (!e) break;
            p = e + 1;
        }
        if ((int)names.size() != kGeneCount)
            Log("gene name list has %zu entries, expected %d", names.size(), kGeneCount);
    }
    return names;
}

int GeneCount()
{
    return kGeneCount;
}

const char* GeneName(int index)
{
    const auto& names = GeneNameList();
    return index >= 0 && index < (int)names.size() ? names[index].c_str() : nullptr;
}

bool GetHorseGenes(int index, float* out, int count)
{
    uint8_t* actor = NthHorseActor(index);
    if (!actor) return false;
    __try {
        uint8_t* horse = *(uint8_t**)(actor + kActorHorse);
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        GeneGetFloatFn geneFloat = (GeneGetFloatFn)(base + kRvaGeneGetFloat);
        int n = count < kGeneCount ? count : kGeneCount;
        for (int g = 0; g < n; g++)
            out[g] = geneFloat(horse + kHorseGenome, g);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static constexpr size_t kAlleleBStride = 0xf0;

bool GetHorseGenome(int index, uint8_t* out)
{
    uint8_t* actor = NthHorseActor(index);
    if (!actor) return false;
    __try {
        uint8_t* horse = *(uint8_t**)(actor + kActorHorse);
        memcpy(out, horse + kHorseGenome, kGenomeBytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetHorseGenome(int index, const uint8_t* genome, const char** err)
{
    *err = nullptr;
    for (int b = 0; b < kGenomeBytes; b++) {
        if (genome[b] > 3) {
            *err = "every allele must be 0 to 3";
            return false;
        }
    }
    uint8_t* actor = NthHorseActor(index);
    if (!actor) {
        *err = "no horse at that position";
        return false;
    }
    uint8_t buf[kGenomeBytes];
    memcpy(buf, genome, kGenomeBytes);
    __try {
        uint8_t* horse = *(uint8_t**)(actor + kActorHorse);
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        HorseSetGenesFn setGenes = (HorseSetGenesFn)(base + kRvaHorseSetGenes);
        HorseApplyGenesFn applyGenes = (HorseApplyGenesFn)(base + kRvaHorseApplyGenes);
        setGenes(horse, buf);
        applyGenes(horse);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *err = "the game refused the genome";
        return false;
    }
    Log("set the genome of the horse at position %d", index + 1);
    return true;
}

uintptr_t HorseActor(int index)
{
    return (uintptr_t)NthHorseActor(index);
}

int FindHorseByActor(uintptr_t actor)
{
    if (!actor) return -1;
    int n = HorseCount();
    for (int i = 0; i < n; i++) {
        if ((uintptr_t)NthHorseActor(i) == actor) return i;
    }
    return -1;
}

bool GetHorseGenomeByActor(uintptr_t actor, int recordId, uint8_t* out)
{
    int index = FindHorseByActor(actor);
    if (index < 0 || !ActorHasRecord((uint8_t*)actor, recordId)) return false;
    return GetHorseGenome(index, out);
}

bool SetHorseGenomeByActor(uintptr_t actor, int recordId, const uint8_t* genome, const char** err)
{
    *err = nullptr;
    int index = FindHorseByActor(actor);
    if (index < 0) {
        *err = "that horse is no longer in the world";
        return false;
    }
    if (!ActorHasRecord((uint8_t*)actor, recordId)) {
        *err = "that horse changed, refresh and try again";
        return false;
    }
    return SetHorseGenome(index, genome, err);
}

static bool ReadAttr(const std::string& line, const char* key, std::string* out)
{
    std::string needle = std::string(" ") + key + "=\"";
    size_t p = line.find(needle);
    if (p == std::string::npos) return false;
    p += needle.size();
    size_t e = line.find('"', p);
    if (e == std::string::npos) return false;
    *out = line.substr(p, e - p);
    return true;
}

static std::vector<GeneDef> g_geneDefs;
static bool g_geneDefsLoaded = false;

static void LoadGeneDefs()
{
    g_geneDefsLoaded = true;
    g_geneDefs.assign(kGeneCount, GeneDef{});

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe);
    path = path.substr(0, path.find_last_of(L"\\/") + 1) + L"data\\genes.xml";
    std::ifstream in(path);
    if (!in) {
        Log("genes.xml not found, gene definitions unavailable");
        return;
    }

    std::map<std::string, GeneDef> byName;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("<gene ") == std::string::npos) continue;
        GeneDef d;
        std::string name, v;
        if (!ReadAttr(line, "name", &name)) continue;
        d.name = name;
        if (ReadAttr(line, "m", &v)) d.m = atoi(v.c_str());
        if (ReadAttr(line, "s", &v)) d.s = atoi(v.c_str());
        for (int k = 0; k < 4; k++) {
            std::string key = "g" + std::to_string(k);
            if (ReadAttr(line, key.c_str(), &v)) d.value[k] = (float)atof(v.c_str());
        }
        ReadAttr(line, "n", &d.letters);
        byName[name] = d;
    }

    int found = 0;
    for (int i = 0; i < kGeneCount; i++) {
        auto it = byName.find(GeneName(i));
        if (it == byName.end()) {
            Log("genes.xml has no gene %s", GeneName(i));
            continue;
        }
        g_geneDefs[i] = it->second;
        found++;
    }
    Log("genes.xml: %d of %d genes loaded", found, kGeneCount);

    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    GeneGetFloatFn geneFloat = (GeneGetFloatFn)(base + kRvaGeneGetFloat);
    int mismatches = 0;
    for (int i = 0; i < kGeneCount; i++) {
        if (g_geneDefs[i].name.empty()) continue;
        for (int k = 0; k < 4; k++) {
            uint8_t genome[kGenomeBytes] = {};
            genome[i] = (uint8_t)k;
            genome[i + kAlleleBStride] = (uint8_t)k;
            float got = geneFloat(genome, i);
            if (fabsf(got - g_geneDefs[i].value[k]) > 0.001f) mismatches++;
        }
    }
    Log("genes.xml vs game table: %d mismatched values", mismatches);
}

bool GetGeneDef(int index, GeneDef* out)
{
    if (!g_geneDefsLoaded) LoadGeneDefs();
    if (index < 0 || index >= (int)g_geneDefs.size() || g_geneDefs[index].name.empty()) return false;
    *out = g_geneDefs[index];
    return true;
}

static constexpr size_t kNodeIsNil = 0x19;
static constexpr size_t kNodeKey = 0x20;

int EnumSpecies(const char** out, int max)
{
    if (!g_popMapPtr || !*g_popMapPtr) return 0;
    int n = 0;
    __try {
        uint8_t* head = *g_popMapPtr;
        uint8_t* stack[64];
        int sp = 0;
        uint8_t* cur = *(uint8_t**)(head + 8);
        while ((*(char*)(cur + kNodeIsNil) == 0 || sp > 0) && n < max) {
            while (*(char*)(cur + kNodeIsNil) == 0) {
                if (sp == 64) return n;
                stack[sp++] = cur;
                cur = *(uint8_t**)cur;
            }
            cur = stack[--sp];
            uint8_t* key = cur + kNodeKey;
            size_t cap = *(size_t*)(key + 0x18);
            out[n++] = cap > 0xf ? *(const char**)key : (const char*)key;
            cur = *(uint8_t**)(cur + 0x10);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return n;
}

static bool SpeciesExists(const char* name)
{
    const char* names[512];
    int n = EnumSpecies(names, 512);
    for (int i = 0; i < n; i++)
        if (!strcmp(names[i], name)) return true;
    return false;
}

bool GetWorldSize(int* w, int* h)
{
    if (!GameReady()) return false;
    *w = *(int*)(*g_worldPtr + 0x278);
    *h = *(int*)(*g_worldPtr + 0x27c);
    return true;
}

static void MakeString(MsvcString* s, const char* text)
{
    memset(s, 0, sizeof(*s));
    size_t n = strlen(text);
    memcpy(s->buf, text, n);
    s->size = n;
    s->cap = 15;
}

const char* SpawnHerd(const char* species, int tx, int ty, int count, int radius, const char* herd)
{
    if (!g_spawnHerd || !g_popMapPtr) return "spawning unavailable (signature not found)";
    if (!GameReady()) return "world not ready";
    size_t sl = strlen(species);
    if (sl == 0 || sl > 15) return "species name must be 1-15 characters";
    if (strlen(herd) > 15) return "herd name must be at most 15 characters";
    if (count < 1 || count > 50) return "count must be 1-50";
    if (radius < 0 || radius > 30) return "radius must be 0-30";
    if (!SpeciesExists(species)) return "unknown species (see shim.species())";
    int w, h;
    GetWorldSize(&w, &h);
    if (tx <= 0 || ty <= 0 || tx >= w - 1 || ty >= h - 1) return "tile out of range (see shim.world_size())";

    MsvcString sp, hn;
    MakeString(&sp, species);
    MakeString(&hn, herd);
    __try {
        g_spawnHerd(*g_worldPtr, tx, ty, &sp, count, radius, &hn);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "game faulted while spawning (caught)";
    }
    return nullptr;
}

using HorseDrawFn = void (*)(void* horse, float scale, char thought);
using PushTranslateFn = void (*)(unsigned long long packed);
using PopTransformFn = void (*)();
using SdlCreateTextureFn = void* (*)(void* r, unsigned fmt, int access, int w, int h);
using SdlDestroyFn = void (*)(void*);
using SdlSetTargetFn = bool (*)(void* r, void* tex);
using SdlGetTargetFn = void* (*)(void* r);
using SdlSetColorFn = bool (*)(void* r, unsigned char red, unsigned char g, unsigned char b, unsigned char a);
using SdlGetColorFn = bool (*)(void* r, unsigned char* red, unsigned char* g, unsigned char* b, unsigned char* a);
using SdlClearFn = bool (*)(void* r);
using SdlReadPixelsFn = void* (*)(void* r, const void* rect);
using SdlSavePngFn = bool (*)(void* surface, const char* file);
using SdlBlendFn = bool (*)(void* tex, unsigned mode);

static HorseDrawFn g_horseDraw;
static PushTranslateFn g_pushTranslate;
static PopTransformFn g_popTransform;
using TransformScaleFn = void (*)(uint32_t sx, uint32_t sy);
static constexpr uintptr_t kRvaTransformScale = 0x74550;
static TransformScaleFn g_transformScale;
static uint8_t** g_rendererPtr;
static SdlCreateTextureFn sdlCreateTexture;
static SdlDestroyFn sdlDestroyTexture;
static SdlDestroyFn sdlDestroySurface;
static SdlSetTargetFn sdlSetTarget;
static SdlGetTargetFn sdlGetTarget;
static SdlSetColorFn sdlSetColor;
static SdlGetColorFn sdlGetColor;
static SdlClearFn sdlClear;
static SdlReadPixelsFn sdlReadPixels;
static SdlSavePngFn sdlSavePng;
static SdlBlendFn sdlBlend;
static bool g_captureTried;
static bool g_captureOk;

static bool CaptureInit()
{
    if (g_captureTried) return g_captureOk;
    g_captureTried = true;

    uintptr_t draw = FindPattern(
        "48 8B C4 48 89 58 10 48 89 70 18 55 57 41 54 41 56 41 57 48 8D 68 B8 48 81 EC 20 01 00 00 0F 29 70 C8 0F 29 78 B8");
    uintptr_t push = FindPattern(
        "48 83 EC 38 48 8B 15 ? ? ? ? 48 3B 15 ? ? ? ? 48 89 4C 24 20 74 3B 0F 10 05");
    uintptr_t pop = FindPattern(
        "48 8B 0D ? ? ? ? 48 39 0D ? ? ? ? 74 2A 0F 10 41 DC 0F 11 05 ? ? ? ? 0F 10 49 EC");
    if (!pop) {
        uint8_t* cand = (uint8_t*)GetModuleHandleW(nullptr) + 0x73150;
        static const uint8_t kPopPrefix[] = { 0x48, 0x8B, 0x0D, 0, 0, 0, 0, 0x48, 0x39, 0x0D };
        bool same = true;
        for (size_t k = 0; k < 3; k++) if (cand[k] != kPopPrefix[k]) same = false;
        Log("capture: pop_transform bytes at RVA 0x73150: %02X %02X %02X %02X %02X %02X %02X %02X",
            cand[0], cand[1], cand[2], cand[3], cand[4], cand[5], cand[6], cand[7]);
        if (same) pop = (uintptr_t)cand;
    }
    uintptr_t rend = FindPattern(
        "48 83 EC 28 48 8B 05 ? ? ? ? 48 85 C0 74 05 48 83 C4 28 C3 48 8D 0D");

    if (!draw) Log("capture: horse_draw signature not found");
    if (!push) Log("capture: push_translate_packed signature not found");
    if (!pop) Log("capture: pop_transform signature not found");
    if (!rend) Log("capture: get_renderer signature not found");
    if (!draw || !push || !pop || !rend) return false;

    g_horseDraw = (HorseDrawFn)draw;
    g_pushTranslate = (PushTranslateFn)push;
    g_popTransform = (PopTransformFn)pop;
    g_transformScale = (TransformScaleFn)((uintptr_t)GetModuleHandleW(nullptr) + kRvaTransformScale);
    g_rendererPtr = (uint8_t**)(rend + 11 + *(int32_t*)(rend + 7));

    HMODULE exe = GetModuleHandleW(nullptr);
    sdlCreateTexture = (SdlCreateTextureFn)GetProcAddress(exe, "SDL_CreateTexture");
    sdlDestroyTexture = (SdlDestroyFn)GetProcAddress(exe, "SDL_DestroyTexture");
    sdlDestroySurface = (SdlDestroyFn)GetProcAddress(exe, "SDL_DestroySurface");
    sdlSetTarget = (SdlSetTargetFn)GetProcAddress(exe, "SDL_SetRenderTarget");
    sdlGetTarget = (SdlGetTargetFn)GetProcAddress(exe, "SDL_GetRenderTarget");
    sdlSetColor = (SdlSetColorFn)GetProcAddress(exe, "SDL_SetRenderDrawColor");
    sdlGetColor = (SdlGetColorFn)GetProcAddress(exe, "SDL_GetRenderDrawColor");
    sdlClear = (SdlClearFn)GetProcAddress(exe, "SDL_RenderClear");
    sdlReadPixels = (SdlReadPixelsFn)GetProcAddress(exe, "SDL_RenderReadPixels");
    sdlSavePng = (SdlSavePngFn)GetProcAddress(exe, "SDL_SavePNG");
    sdlBlend = (SdlBlendFn)GetProcAddress(exe, "SDL_SetTextureBlendMode");

    if (!sdlCreateTexture || !sdlDestroyTexture || !sdlDestroySurface || !sdlSetTarget || !sdlGetTarget ||
        !sdlSetColor || !sdlGetColor || !sdlClear || !sdlReadPixels || !sdlSavePng || !sdlBlend) {
        Log("capture: an SDL export is missing");
        return false;
    }
    Log("capture ready: horse_draw %p renderer global %p", (void*)draw, (void*)g_rendererPtr);
    g_captureOk = true;
    return true;
}

static bool DrawHorseGuarded(void* horse, float scale, float tx, float ty)
{
    unsigned long long packed;
    std::memcpy(&packed, &tx, 4);
    std::memcpy(((char*)&packed) + 4, &ty, 4);
    __try {
        g_pushTranslate(packed);
        g_horseDraw(horse, scale, 0);
        g_popTransform();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static unsigned long long PackPoint(float x, float y)
{
    unsigned long long packed;
    std::memcpy(&packed, &x, 4);
    std::memcpy(((char*)&packed) + 4, &y, 4);
    return packed;
}

static bool DrawHorseZoomed(void* horse, float zoom, float cx, float cy, float bodyX, float bodyY)
{
    uint32_t zoomBits;
    std::memcpy(&zoomBits, &zoom, 4);
    __try {
        g_pushTranslate(PackPoint(cx, cy));
        g_transformScale(zoomBits, zoomBits);
        g_pushTranslate(PackPoint(-bodyX, -bodyY));
        g_horseDraw(horse, 1.0f, 0);
        g_popTransform();
        g_popTransform();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static constexpr uintptr_t kRvaHorseGetPosition = 0xb4ab0;
using HorseGetPositionFn = float* (*)(void* horse, float* out);

bool CaptureHorse(int index, const char* path, int size, float scale, float ox, float oy, bool local, const char** err)
{
    if (!CaptureInit()) { *err = "capture signatures not found, see SHIM.log"; return false; }
    if (size < 16 || size > 2048) { *err = "size must be 16 to 2048"; return false; }
    uint8_t* actor = NthHorseActor(index);
    if (!actor) { *err = "no horse at that position"; return false; }
    void* horse = nullptr;
    float px = 0, py = 0;
    __try {
        horse = *(void**)(actor + kActorHorse);
        px = *(float*)(actor + kActorX);
        py = *(float*)(actor + kActorX + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *err = "could not read horse";
        return false;
    }
    void* renderer = g_rendererPtr ? *g_rendererPtr : nullptr;
    if (!horse || !renderer) { *err = "renderer or horse not ready"; return false; }

    void* tex = sdlCreateTexture(renderer, 0x16362004u, 2, size, size);
    if (!tex) { *err = "could not create target texture"; return false; }
    sdlBlend(tex, 1);

    void* prevTarget = sdlGetTarget(renderer);
    unsigned char r = 0, g = 0, b = 0, a = 255;
    sdlGetColor(renderer, &r, &g, &b, &a);

    float bodyX = px, bodyY = py;
    if (!local) {
        float body[2] = { px, py };
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        HorseGetPositionFn getPos = (HorseGetPositionFn)(base + kRvaHorseGetPosition);
        __try {
            getPos(horse, body);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        bodyX = body[0] + *(float*)((char*)horse + 0x1e4);
        bodyY = body[1] + *(float*)((char*)horse + 0x1e8);
    }
    float half = size * 0.5f;
    float tx = local ? half + ox : half + ox - bodyX;
    float ty = local ? half + oy : half + oy - bodyY;

    bool drew = false;
    bool targetSet = sdlSetTarget(renderer, tex);
    Log("capture diag: idx %d actor %.2f,%.2f body %.2f,%.2f tx %.2f ty %.2f half %.1f scale %.2f local %d target %d",
        index, px, py, bodyX, bodyY, tx, ty, half, scale, local ? 1 : 0, targetSet ? 1 : 0);
    if (targetSet) {
        sdlSetColor(renderer, 0, 0, 0, 0);
        sdlClear(renderer);
        drew = DrawHorseGuarded(horse, scale, tx, ty);
    }
    Log("capture diag: drew %d", drew ? 1 : 0);
    void* surf = drew ? sdlReadPixels(renderer, nullptr) : nullptr;
    Log("capture diag: surface %p", surf);
    if (surf) {
        uint8_t* spx = *(uint8_t**)((uint8_t*)surf + 24);
        int sw = *(int*)((uint8_t*)surf + 8);
        int sh = *(int*)((uint8_t*)surf + 12);
        int sp = *(int*)((uint8_t*)surf + 16);
        int opaque = 0, minx = 1 << 30, miny = 1 << 30, maxx = -1, maxy = -1;
        if (spx) {
            for (int yy = 0; yy < sh; yy++) {
                for (int xx = 0; xx < sw; xx++) {
                    if (spx[yy * sp + xx * 4 + 3] != 0) {
                        opaque++;
                        if (xx < minx) minx = xx;
                        if (xx > maxx) maxx = xx;
                        if (yy < miny) miny = yy;
                        if (yy > maxy) maxy = yy;
                    }
                }
            }
        }
        Log("capture diag: surf %dx%d pitch %d pixels %p opaque %d box %d,%d-%d,%d",
            sw, sh, sp, (void*)spx, opaque, minx, miny, maxx, maxy);
    }

    sdlSetTarget(renderer, prevTarget);
    sdlSetColor(renderer, r, g, b, a);
    sdlDestroyTexture(tex);

    if (!drew) { *err = "horse_draw faulted"; return false; }
    if (!surf) { *err = "read pixels failed"; return false; }
    bool saved = sdlSavePng(surf, path);
    sdlDestroySurface(surf);
    if (!saved) { *err = "could not write png"; return false; }
    return true;
}
