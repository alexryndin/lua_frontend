global G
local<const> a, b<const> = 1, 2
local t = {x = 1, [2] = 3, 4; 5}

::again::
a = a + 1 * 2 ^ 3
if a < b then
  t.x = a
elseif a == b then
  t[1] = b
else
  t[2] = nil
end

while a < 20 do a = a + 1 end
repeat b = b - 1 until b == 0
for i = 1, 10, 2 do t[i] = i end
for k, v in pairs(t) do G = v end

local f = function(x, ... rest)
  return x, rest.n
end

function t:m(x)
  return self.x + x
end

goto again
