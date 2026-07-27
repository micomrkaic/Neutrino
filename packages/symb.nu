% symb.nu — symbolic differentiation in pure Neutrino.
%
% Expressions are nested records built by constructors (there is no parser
% on purpose: the string builtins have no substring access, so expression
% entry is constructor-style, in the spirit of RPN):
%
%   let e = add(powc(X, 3), mul(C(5), sinx(X)))    % x^3 + 5 sin x
%   show(simp(ddx(e)))                             % "((3 * x^2) + (5 * cos(x)))"
%
% ddx (not diff — diff is the array builtin) differentiates by structural
% recursion. sub and divx are desugared into add/mul/powc at construction,
% so the product, power, and chain rules alone carry the whole calculus —
% the quotient rule falls out of d(b^-1) for free.

% ---- constructors ----
let C = fn v -> {op = "const", v = v}
let X = {op = "var"}
let add = fn a, b -> {op = "add", l = a, r = b}
let mul = fn a, b -> {op = "mul", l = a, r = b}
let powc = fn a, n -> {op = "pow", l = a, n = n}
let sinx = fn a -> {op = "sin", l = a}
let cosx = fn a -> {op = "cos", l = a}
let tanx = fn a -> {op = "tan", l = a}
let expx = fn a -> {op = "exp", l = a}
let logx = fn a -> {op = "log", l = a}
let sub = fn a, b -> add(a, mul(C(-1), b))
let divx = fn a, b -> mul(a, powc(b, -1))
let sqrtx = fn a -> powc(a, 0.5)

% ---- the derivative ----
let ddx = fn e -> (
  if e.op == "const" then C(0)
  else if e.op == "var" then C(1)
  else if e.op == "add" then add(ddx(e.l), ddx(e.r))
  else if e.op == "mul" then add(mul(ddx(e.l), e.r), mul(e.l, ddx(e.r)))
  else if e.op == "pow" then mul(mul(C(e.n), powc(e.l, e.n - 1)), ddx(e.l))
  else if e.op == "sin" then mul(cosx(e.l), ddx(e.l))
  else if e.op == "cos" then mul(mul(C(-1), sinx(e.l)), ddx(e.l))
  else if e.op == "tan" then mul(add(C(1), powc(tanx(e.l), 2)), ddx(e.l))
  else if e.op == "log" then mul(powc(e.l, -1), ddx(e.l))
  else mul(expx(e.l), ddx(e.l))
  end end end end end end end end end
)

% ---- evaluation at a point ----
let evalx = fn e, x -> (
  if e.op == "const" then e.v
  else if e.op == "var" then x
  else if e.op == "add" then evalx(e.l, x) + evalx(e.r, x)
  else if e.op == "mul" then evalx(e.l, x) * evalx(e.r, x)
  else if e.op == "pow" then evalx(e.l, x) ^ e.n
  else if e.op == "sin" then sin(evalx(e.l, x))
  else if e.op == "cos" then cos(evalx(e.l, x))
  else if e.op == "tan" then tan(evalx(e.l, x))
  else if e.op == "log" then log(evalx(e.l, x))
  else exp(evalx(e.l, x))
  end end end end end end end end end
)

% ---- substitution: replace the variable with another expression ----
let subst = fn e, g -> (
  if e.op == "const" then e
  else if e.op == "var" then g
  else if e.op == "add" then add(subst(e.l, g), subst(e.r, g))
  else if e.op == "mul" then mul(subst(e.l, g), subst(e.r, g))
  else if e.op == "pow" then powc(subst(e.l, g), e.n)
  else {op = e.op, l = subst(e.l, g)}
  end end end end end
)

% ---- simplifier: fold constants, strip identities, pull constants left ----
let simp = fn e -> (
  if e.op == "add" then (
    let a = simp(e.l); let b = simp(e.r);
    if a.op == "const" then (
      if b.op == "const" then C(a.v + b.v)
      else if a.v == 0 then b
      else add(a, b) end end
    )
    else if b.op == "const" then (if b.v == 0 then a else add(a, b) end)
    else add(a, b)
    end end
  )
  else if e.op == "mul" then (
    let a = simp(e.l); let b = simp(e.r);
    if a.op == "const" then (
      if b.op == "const" then C(a.v * b.v)
      else if a.v == 0 then C(0)
      else if a.v == 1 then b
      else if b.op == "mul" then (
        if b.l.op == "const" then mul(C(a.v * b.l.v), b.r) else mul(a, b) end
      )
      else mul(a, b) end end end end
    )
    else if b.op == "const" then (
      if b.v == 0 then C(0) else if b.v == 1 then a else mul(b, a) end end
    )
    else mul(a, b)
    end end
  )
  else if e.op == "pow" then (
    let a = simp(e.l);
    if e.n == 0 then C(1)
    else if e.n == 1 then a
    else if a.op == "const" then C(a.v ^ e.n)
    else powc(a, e.n)
    end end end
  )
  else if e.op == "const" then e
  else if e.op == "var" then e
  else {op = e.op, l = simp(e.l)}
  end end end end end
)

% ---- pretty printer ----
let show = fn e -> (
  if e.op == "const" then str(e.v)
  else if e.op == "var" then "x"
  else if e.op == "add" then "(" + show(e.l) + " + " + show(e.r) + ")"
  else if e.op == "mul" then "(" + show(e.l) + " * " + show(e.r) + ")"
  else if e.op == "pow" then show(e.l) + "^" + str(e.n)
  else e.op + "(" + show(e.l) + ")"
  end end end end end
)

% ---- the k-th derivative, simplified as it goes ----
let dn = fn e, k -> if k <= 0 then simp(e) else dn(simp(ddx(e)), k - 1) end

% ---- Taylor coefficients about 0: [c0, c1, ..., cn], f ~ sum ck x^k ----
let taylor = fn e, n -> (
  0:n ~> (fn k -> evalx(dn(e, k), 0) / (if k == 0 then 1 else prod[j = 1:k] j end))
)
