#pragma once

void LuaInit();
void LuaTick();
// wird vom game-hook aufgerufen, Lua laeuft erst im LuaTick
void LuaQueueHorseBorn(int childId, int parentA, int parentB);
