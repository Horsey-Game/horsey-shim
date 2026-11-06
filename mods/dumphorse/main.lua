local list = shim.horses()
local h = list[1]
local g = shim.genes(1)
local names = {}
for k in pairs(g) do names[#names + 1] = k end
table.sort(names)
local out = { "id=" .. tostring(h.id), "name=" .. tostring(h.name), "genome=" .. shim.horse_genome(1) }
for _, k in ipairs(names) do out[#out + 1] = k .. "=" .. tostring(g[k]) end
shim.write_file("horse_" .. tostring(h.id) .. ".txt", table.concat(out, "\n"))
