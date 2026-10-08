global <const> *

local function many() return 11, 22, 33 end
local a, b, c = many()
print("multi", a, b, c)

local p, q = (many())
print("paren", p, q == nil)

local t = {many()}
local u = {(many())}
print("table", #t, t[1], t[2], t[3], #u, u[1])

local function named (... rest)
  return rest.n, rest[1], rest[2], rest[3]
end
print("named", named(many()))

local hit = 0
local function bump() hit = hit + 1; return true end
local x = false and bump()
local y = true or bump()
print("short", x, y, hit)

local function tail(n, acc)
  if n == 0 then return acc end
  return tail(n - 1, acc + n)
end
print("tail", tail(50, 0))

local sum = 0
for i = 1, 5 do sum = sum + i end
for _, v in ipairs({2, 4, 6}) do sum = sum + v end
print("loops", sum)

do
  local z <const> = 7
  print("const", z)
end

local g = 0
::again::
g = g + 1
if g < 2 then goto again end
print("goto", g)
