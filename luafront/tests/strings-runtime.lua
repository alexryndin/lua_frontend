local a = "a\0b"
assert(#a == 3 and string.byte(a, 1) == 97 and string.byte(a, 2) == 0 and string.byte(a, 3) == 98)
local b = "\x41\065\u{42}"
assert(b == "AAB")
local c = "x\z   
	  y"
assert(c == "xy")
local d = "left\
right"
assert(d == "left\nright")
local e = [[
first
secondthird
fourth
fifth]]
assert(e == "first\nsecond\nthird\nfourth\nfifth")
print("strings-runtime-ok")
