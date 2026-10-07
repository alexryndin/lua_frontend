local x = 40
local f = load("return 2")
assert(x + f() == 42)

local blob = string.dump(function (v) return v + 1 end)
local g = assert(load(blob))
assert(g(41) == 42)

print("standard-runtime-ok")
