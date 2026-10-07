-- line one
local a = 1 --[[ inline ]] + 2;
local b = --[==[ level-2
long comment ]==] 3;
--[x bracket-ish text: still a line comment
local c = 4;
local function f(x, y) return x * 10 + y; end
print(f(--[[ between args ]] a, b), c) -- trailing comment
-- eof comment without newline