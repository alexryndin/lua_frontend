let x = 10;
let y = 20;

function max(a, b) {
  if (a > b) {
    return a;
  } else {
    return b;
  }
}

assert(max(x, y) == 20);

let loaded = require("syntax").load("let q = 40; return q + 2;", "jslike");
assert(loaded() == 42);

let from_file = loadfile("tests/js-loaded.lua");
assert(from_file() == 123);

package.path = "tests/?.lua;tests/?.ljs";
let m = require("jsmodule");
assert(m.value == 99);

let blob = string.dump(function(v) { return v + 1; });
let binary = load(blob);
assert(binary(41) == 42);

print("js-runtime-ok");
