global G, pairs
local<const> a, b<const> = 1, 2
local t = {x = 1, [2] = 3, 4; 5}
local n = 0

::again::
n = n + 1 * 2 ^ 3
if n < b then
  t.x = a
elseif n == b then
  t[1] = b
else
  t[2] = nil
end

while n < 20 do n = n + 1 end
repeat n = n - 1 until n == 0
for i = 1, 10, 2 do t[i] = i end
for k, v in pairs(t) do G = v end

local f = function(x, ... rest)
  return x, rest.n
end

function t:m(x)
  return self.x + x
end

if a < b then goto again end
