# SHIM Lua API

SHIM loads every `mods/<name>/main.lua` from the game folder. All functions live in the
global `shim` table. Output from `shim.log` goes to `SHIM.log` and the SHIM console.

Mods reload automatically when a `main.lua` changes (about 1 s delay). Create
`shim_autoreload.off` in the game folder to disable that, or `shim_console.off` to
disable the console window. `shim.reload()` reloads on demand.

## Events

```lua
shim.on("ready", function() end)        -- once, when the world is first available (again after reload)
shim.on("tick", function() end)         -- every game tick (~65/s)
shim.on("horse_added", function(h) end) -- a horse actor appeared in the world
shim.on("horse_removed", function(h) end) -- a horse actor left the world (h = last known state)
shim.on("location_changed", function(from, to, from_id, to_id) end) -- player entered or left a location
```

`location_changed` fires when the player goes into or out of a location (a barn, a store...).
`from` and `to` are names, e.g. `"map"` or the location's name; `from_id` and `to_id` are the
ids from `shim.active_scene()`. The change is also written to `SHIM.log`.

Horse events are checked about every 0.5 s. Horses already present when the world loads
do not fire `horse_added`. If more than 100 horses change in one check (a save load), SHIM
treats it as a world reset and fires nothing.

A handler that raises an error is logged with a traceback and removed.

## Timers

```lua
local id = shim.after(ticks, fn)  -- run once after `ticks` ticks
local id = shim.every(ticks, fn)  -- run every `ticks` ticks
shim.cancel(id)                   -- returns true if the timer existed
```

## Money

```lua
shim.get_money()              -- integer
shim.set_money(n)
shim.add_money(n, silent)     -- through the game's own function (popups unless silent)
shim.spend_money(n, silent)   -- returns true if the player could afford it
```

## Horses

```lua
local list = shim.horses()    -- array of horse tables, includes wild horses
shim.set_horse(i, field, v)   -- i is the 1-based position in the last horses() result
                              -- fields: "hungry", "tired" (boolean), "age" (integer)
shim.set_rest(i)              -- makes the horse at position i tired and needing rest, the way the
                              -- game does it. Returns true, or false if there is no such horse
                              -- or the horse is in state 6, which the game ignores
shim.genes(i)                 -- read only: table of gene name -> expressed value (float),
                              -- for the horse at position i (same numbering as horses())
shim.compare(i, j)            -- read only: text listing the genes that differ between horses
                              -- i and j, one per line as "NAME a -> b", headed by the count
shim.set_name(id, name)       -- name the horse with record id (h.id), 1 to 15 characters.
                              -- Returns true, or false plus a reason
shim.record(id)               -- read only: the horse record with this id (use h.id), or nil.
                              -- Fields: id, name, parent_a, parent_b (-1 if none), children
                              -- (array of record ids bred from this horse), special (1 for
                              -- prize-box horses, else 0), color_a and color_b (colors the game
                              -- gives scene NPCs; 0 or -1 for horses).
                              -- Known gap: horses() hides the name when special is 1, so prize
                              -- horses may show a blank name there. Not yet checked in-game.
```

`shim.genes(i)` gives all 240 genes, e.g. `shim.genes(1).SIZE` or `shim.genes(1).HAT_EXISTS`.
Values are the game's own expressed values, the ones it uses when building the horse's body.
Scales differ between genes, so check the value range for a gene before comparing horses.

A horse table has: `id`, `name`, `state`, `age`, `max_age`, `hungry`, `tired`, `old`,
`wild`, `x`, `y`.

- `id` is the horse record id. `-1` means the actor has no record (ambient horses);
  these are numerous and come and go constantly.
- `name` is empty for horses that have not been named.
- `wild` is true for wild horses.
- `i` for `set_horse` is only valid until the horse list changes, so call `horses()` and
  `set_horse()` in the same handler.

## World

```lua
shim.active_scene()           -- id of the location the player is in (a building, barn, store...).
                              -- -1 means the player is not inside a location, e.g. on the map
shim.location_name(id)        -- name of a location id; -1 is "map"
shim.world_size()             -- width, height in tiles (a tile is 32 world units)
shim.frame()                  -- the game's frame counter (increases every tick)
shim.species()                -- sorted array of population names, e.g. "fish", "dino", "bear"
```

## Spawning

```lua
local id = shim.spawn_recorded(species, tile_x, tile_y)  -- one horse with a record
```

Spawns one horse and gives it a new horse record, so it has a real `id` and `shim.record(id)`
works. Returns the record id, or `false` plus a reason. It spawns a single horse, with no
parents and no name. Test on a debug save first, since this is the first time it has been run.

```lua
local id, err = shim.clone(i)  -- clone the horse at position i (from horses())
```

`shim.clone_id(record_id)` does the same, but finds the horse by its record id (`h.id`) instead of
its list position. It only finds horses on the map.

Makes a new recorded horse with the same genome as horse `i`, on the same tile. It uses the
`default` species and the game's own body rebuild, so the copy should look the same. Check it
with `shim.genes(i)` against `shim.genes(new position)`. Returns the new record id, or `false`
plus a reason. Test on a debug save first.

```lua
local ok, err = shim.spawn(species, tile_x, tile_y [, count [, radius [, herd]]])
```

Spawns `count` (default 1) creatures at random tiles within `radius` (default 0) of the tile.
`herd` is an optional herd name, at most 15 characters, and defaults to the species name.
Returns `true`, or `false` plus a reason. Species must come from `shim.species()`; unknown
names are rejected because the game itself exits on them. Limits: species name 1–15
characters, count 1–50, radius 0–30, tile inside the world. Every call is logged to
`SHIM.log` with the result, the scene and the horse count before and after.

Tiles: `tile = floor(x / 32)`.

```lua
shim.spawn("fish", 100, 100)             -- one fish at tile 100,100
shim.spawn("centipede", 100, 100, 5, 3)  -- five within 3 tiles
```

## Config

```lua
local cfg = shim.config({ speed = 2, enabled = true })
```

Call at the top of a mod. Reads `mods/<name>/config.lua` (a file that returns a table),
overlays its values on your defaults, writes the merged result back so new options show up
in the file, and returns it. Supports numbers, booleans and strings.

## Console

A console window opens with the game. Type Lua; expressions print their value.

```
> shim.get_money()
> #shim.horses()
> shim.set_money(1000)
```
