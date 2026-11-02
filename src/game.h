
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct RecordInfo {
    int id;
    std::string name;
    int special;
    int colorA, colorB;
    int parentA, parentB;
    std::vector<int> children;
};

bool GetRecord(int id, RecordInfo* out);

int SpawnRecordedHorse(const char* species, int tx, int ty, const char** err, void** horseOut = nullptr);

int CloneHorse(int index, const char** err);

int CloneHorseById(int recordId, const char** err);

bool SetRecordName(int recordId, const char* name, const char** err);

std::string CompareGenes(int a, int b, const char** err);

struct HorseInfo {
    int id;
    const char* name;
    int state;
    int age;
    int maxAge;
    bool hungry;
    bool tired;
    bool old;
    bool wild;
    float x, y;
};

bool GameInit();
bool GameReady();
int GetMoney();
void SetMoney(int value);
void AddMoney(int amount, bool silent);
bool SpendMoney(int amount, bool silent);

int HorseCount();
bool GetHorse(int index, HorseInfo* out);
int EnumHorseActors(void** out, int max);
bool GetHorseFromActor(void* actor, HorseInfo* out);
bool SetHorseField(int index, const char* field, int value);
bool SetHorseRest(int index);

bool GetHorseGenes(int index, float* out, int count);
int GeneCount();
const char* GeneName(int index);

constexpr int kGenomeBytes = 480;
bool GetHorseGenome(int index, uint8_t* out);
bool SetHorseGenome(int index, const uint8_t* genome, const char** err);

uintptr_t HorseActor(int index);
int FindHorseByActor(uintptr_t actor);
bool GetHorseGenomeByActor(uintptr_t actor, int recordId, uint8_t* out);
bool SetHorseGenomeByActor(uintptr_t actor, int recordId, const uint8_t* genome, const char** err);

struct GeneDef {
    std::string name;
    int m = 0;
    int s = 1;
    std::string letters;
    float value[4] = {0, 0, 0, 0};
};
bool GetGeneDef(int index, GeneDef* out);

void InstallBirthHooks();

int ActiveSceneId();
int FrameCounter();
const char* LocationName(int index);
void LogWorldDebug();

int EnumSpecies(const char** out, int max);
bool GetWorldSize(int* w, int* h);
const char* SpawnHerd(const char* species, int tx, int ty, int count, int radius, const char* herd);

bool CaptureHorse(int index, const char* path, int size, float scale, float ox, float oy, bool local, const char** err);
