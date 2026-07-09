# multi-line expressions: newlines are whitespace inside open brackets
let quad = fn a, b, c, x -> (
  a * x^2
  + b * x
  + c)
let M = [1, 2;
         3, 4]
let cfg = {tol = 1e-9,
           title = "run"}
let total = quad(1, 2, 1, 3) + M[2, 2] + cfg.tol
