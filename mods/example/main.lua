-- Example SHIM mod: adds money on a timer, lists your herd on load, and logs horse events.
-- Options live in mods/example/config.lua (created on first run).
local cfg = shim.config({
    interval_ticks = 650, -- 650 ticks is about 10 seconds at 65 FPS
    amount = 1,
    list_horses = true,
    log_events = true,
    log_locations = true,
})

shim.log("example mod loaded")

shim.on("ready", function()
    shim.log("world ready, money =", shim.get_money())
    shim.log("species:", table.concat(shim.species(), ", "))
    -- Try from the console (tiles are 32 world units, so tile = floor(horse.x / 32)):
    --   shim.spawn("fish", 100, 100)             one at tile 100,100
    --   shim.spawn("centipede", 100, 100, 5, 3)  five within 3 tiles
    -- Returns true, or false plus a reason. Unknown species are rejected, because the
    -- game itself exits on one.
    if not cfg.list_horses then return end
    local owned, wild, other = 0, 0, 0
    for i, h in ipairs(shim.horses()) do
        if h.wild then
            wild = wild + 1
        elseif h.id == -1 then
            other = other + 1
        else
            owned = owned + 1
            shim.log(i, h.name, "age", h.age .. "/" .. h.max_age, h.hungry and "hungry" or "", h.tired and "tired" or "")
        end
    end
    shim.log("owned:", owned, "wild:", wild, "no record:", other)
end)

-- Fired (checked every ~0.5 s) when a horse appears in or leaves the world.
-- The handler gets the horse table; for horse_removed it is the last known state.
-- id == -1 means the actor has no horse record (ambient horses), which come and go
-- constantly, so this example only logs horses that have a record.
shim.on("horse_added", function(h)
    if cfg.log_events and h.id ~= -1 then shim.log("horse added:", h.name ~= "" and h.name or "(unnamed)", h.wild and "wild" or "owned", "id", h.id) end
end)

shim.on("horse_removed", function(h)
    if cfg.log_events and h.id ~= -1 then shim.log("horse removed:", h.name ~= "" and h.name or "(unnamed)", h.wild and "wild" or "owned", "id", h.id) end
end)

-- Fired when the player enters or leaves a location (a barn, a store...). The map is "map".
-- Also written to SHIM.log by the game side.
shim.on("location_changed", function(from, to)
    if cfg.log_locations then shim.log("location:", from, "->", to) end
end)

-- shim.every(ticks, fn) repeats; shim.after(ticks, fn) runs once; shim.cancel(id) stops either.
shim.every(cfg.interval_ticks, function()
    shim.add_money(cfg.amount, true)
    shim.log("money =", shim.get_money())
end)
