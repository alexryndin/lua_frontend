local x <const> = 10
local y = 20

function max(a, b, ... rest)
  if a > b then
    return a
  elseif a == b then
    return rest.n
  else
    return b
  end
end

local t = {x = 1, [2] = 3, max(x, y)}
for i = 1, 10, 2 do
  t[i] = max(t[i], i)
end
