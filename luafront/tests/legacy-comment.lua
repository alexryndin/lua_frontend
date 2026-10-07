-- legacy profile without comment directives: the built-in Lua comment set
-- ('--' line + '--' long bracket) must remain active.
-- header comment
let x = 40;
-- middle comment
let y = 2;
return x + y;
--[[ trailing long
comment ]]
