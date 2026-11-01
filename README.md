# horsey shim

this is the shim for the horsey game mod loader. its a version.dll proxy that loads into horsey.exe at startup and runs a lua runtime so mods can hook into the game

the mod manager app on the manager branch bundles a build of this shim and installs it for you so most people never need to build it themselves

gpl-3.0 see LICENSE

this project is not made by or affiliated with the horsey game developers

## how it works

- the game loads version.dll from its own folder. the shim sits there as a proxy for the real windows version api so the game still gets everything it needs
- once loaded the shim finds the game code it needs by signature scan and hooks it with minhook
- lua 5.4.7 is compiled in and each mod runs in its own lua state
- mods live in mods/<name>/main.lua under the game folder
- output goes to SHIM.log next to the game and to a console window

## writing mods

API.md has the full lua api. events timers money horses and world stuff. short version

```lua
shim.on("ready", function()
  shim.log("hello from my mod")
end)
```

mods reload when main.lua changes (about a second later). make a file called shim_autoreload.off in the game folder to turn that off. make shim_console.off to hide the console window

the shim only looks for main.lua. manifest.json is for the mod manager so check the mod loader readme for that format

## building

you need visual studio with the c++ tools. cmake and ninja come with it. then run build.bat or do this from a developer prompt

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl
cmake --build build
```

build/version.dll is the output. cmake downloads minhook and lua 5.4.7 while it configures so you need internet the first time

HORSEY_INSTALL copies the dll and the mods folder into the game folder when you build. its off by default and HORSEY_DIR sets where it goes

## api version

kShimApiVersion is 1 in src/lua_api.cpp. mods declare api_version in their manifest and the shim refuses any mod that needs a newer version. only bump it when the lua api changes in a way old mods cant handle

## status

hobby project so expect rough edges

## credits

thanks to everyone who helped with this

- Kars for the icons and support
- bwhatsonn for ideas
- JumboDS64 for ideas. check out his game Cain Iddo Break Week https://store.steampowered.com/app/4068390/Cain_Iddo_Break_Week/
- cpt2angry for ideas
- everyone in the horsey game discord server