global print, setmetatable
local function ret3() return 1, 2, 3 end
local a,b,c = ret3()
print('multret', a,b,c)
local x,y,z = (ret3())
print('paren', x,y,z)
local t = {ret3()}
print('table-ret', #t, t[1], t[2], t[3])

local function pack(...v)
  return v.n, v[1], v[2], v[3]
end
print('named-vararg', pack('a', nil, 'c'))

local function fact(n)
  if n <= 1 then return 1 end
  return n * fact(n - 1)
end
print('rec', fact(8))

local function counter()
  local n = 0
  return function() n = n + 1; return n end
end
local c1 = counter()
print('upvalue', c1(), c1(), c1())

local obj = {x = 10}
function obj:add(y) return self.x + y end
print('method', obj:add(7))

local q = 0
::again::
q = q + 1
if q < 3 then goto again end
print('goto', q)

print('assoc', 2^3^2, (2^3)^2, 10-(3-1), (10-3)-1)

global g = 11
global function gadd(x) return g + x end
print('global', gadd(4))

do
  local guard <close> = setmetatable({}, {__close=function() print('closed') end})
  print('inside-close')
end
