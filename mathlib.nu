# mathlib.nu — a tiny test package: plain functions + a record-of-closures module
let cube = fn x -> x^3
let mean2 = fn a -> sum(a) / numel(a)
let geo = {
  hyp = fn a -> sqrt(sum(a .^ 2)),
  unit = fn a -> a ./ sqrt(sum(a .^ 2))
}
