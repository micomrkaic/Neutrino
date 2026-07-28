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
    else if b.op == "mul" then (
      if b.l.op == "const" then mul(b.l, mul(a, b.r)) else mul(a, b) end
    )
    else mul(a, b)
    end end end
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

% ---- pretty printer (division- and subtraction-aware) ----
let is_neg1 = fn e -> if e.op == "const" then e.v == -1 else false end
let showdenom = fn b, m -> if m == 1 then show(b) else show(b) + "^" + str(m) end
let show = fn e -> (
  if e.op == "const" then str(e.v)
  else if e.op == "var" then "x"
  else if e.op == "add" then (
    if e.r.op == "mul" then (
      if is_neg1(e.r.l) then "(" + show(e.l) + " - " + show(e.r.r) + ")"
      else "(" + show(e.l) + " + " + show(e.r) + ")" end
    )
    else "(" + show(e.l) + " + " + show(e.r) + ")" end
  )
  else if e.op == "mul" then (
    if is_neg1(e.l) then "(-" + show(e.r) + ")"
    else if e.r.op == "pow" then (
      if e.r.n < 0 then "(" + show(e.l) + " / " + showdenom(e.r.l, -e.r.n) + ")"
      else "(" + show(e.l) + " * " + show(e.r) + ")" end
    )
    else if e.l.op == "pow" then (
      if e.l.n < 0 then "(" + show(e.r) + " / " + showdenom(e.l.l, -e.l.n) + ")"
      else "(" + show(e.l) + " * " + show(e.r) + ")" end
    )
    else "(" + show(e.l) + " * " + show(e.r) + ")" end end end
  )
  else if e.op == "pow" then (
    if e.n < 0 then "(1 / " + showdenom(e.l, -e.n) + ")"
    else show(e.l) + "^" + str(e.n) end
  )
  else e.op + "(" + show(e.l) + ")"
  end end end end end
)

% ---- the k-th derivative, simplified as it goes ----
let dn = fn e, k -> if k <= 0 then simp(e) else dn(simp(ddx(e)), k - 1) end

% ---- Taylor coefficients about 0: [c0, c1, ..., cn], f ~ sum ck x^k ----
let taylor = fn e, n -> (
  0:n ~> (fn k -> evalx(dn(e, k), 0) / (if k == 0 then 1 else prod[j = 1:k] j end))
)

% ---- the parser that was possible all along ----
% (v2.12.1 recorded string extraction as impossible; v2.13.1 corrected the
% record: strings index like arrays, and always did. This parser is the
% correction made executable: recursive descent over s[i], character
% classes as chained comparisons, expression grammar with ^ right-assoc.
% General powers desugar as f^g = exp(g*log(f)) — fully differentiable.
% Grammar: numbers, x, pi, + - * / ^ ( ), sin cos tan exp log sqrt.)
let fail = fn msg -> num("symb parse error: " + msg)
let peek = fn s, i -> if i > length(s) then "" else s[i] end
let skipws = fn s, i -> if peek(s, i) == " " then skipws(s, i + 1) else i end
let isdig = fn c -> if c == "" then false else "0" <= c <= "9" end
let isalp = fn c -> if c == "" then false else "a" <= c <= "z" end
let scannum = fn s, i -> (
  if isdig(peek(s, i)) then scannum(s, i + 1)
  else if peek(s, i) == "." then scannum(s, i + 1)
  else i end end
)
let scanid = fn s, i -> if isalp(peek(s, i)) then scanid(s, i + 1) else i end
let mkfun = fn name, a -> (
  if name == "sin" then sinx(a)
  else if name == "cos" then cosx(a)
  else if name == "tan" then tanx(a)
  else if name == "exp" then expx(a)
  else if name == "log" then logx(a)
  else if name == "sqrt" then sqrtx(a)
  else fail("unknown function " + name)
  end end end end end end
)
let mkpow = fn a, b -> if b.op == "const" then powc(a, b.v) else expx(mul(b, logx(a))) end

let parse_atom = fn s, i0 -> (
  let i = skipws(s, i0);
  let c = peek(s, i);
  if isdig(c) then (
    let j = scannum(s, i);
    {i = j, e = C(num(s[i : j - 1]))}
  )
  else if c == "(" then (
    let r = parse_expr(s, i + 1);
    let j = skipws(s, r.i);
    if peek(s, j) == ")" then {i = j + 1, e = r.e} else fail("expected )") end
  )
  else if c == "-" then (
    let r = parse_pow(s, i + 1);       % minus binds looser than ^: -x^2 is -(x^2)
    {i = r.i, e = mul(C(-1), r.e)}
  )
  else if isalp(c) then (
    let j = scanid(s, i);
    let name = s[i : j - 1];
    if name == "x" then {i = j, e = X}
    else if name == "pi" then {i = j, e = C(pi)}
    else (
      let k = skipws(s, j);
      if peek(s, k) == "(" then (
        let r = parse_expr(s, k + 1);
        let m = skipws(s, r.i);
        if peek(s, m) == ")" then {i = m + 1, e = mkfun(name, r.e)}
        else fail("expected ) after " + name) end
      ) else fail("expected ( after " + name) end
    ) end end
  )
  else fail("unexpected input at position " + str(i))
  end end end end
)

let parse_pow = fn s, i -> (
  let a = parse_atom(s, i);
  let j = skipws(s, a.i);
  if peek(s, j) == "^" then (
    let b = parse_pow(s, j + 1);
    {i = b.i, e = mkpow(a.e, b.e)}
  ) else a end
)

let term_loop = fn s, st -> (
  let j = skipws(s, st.i);
  let c = peek(s, j);
  if c == "*" then (
    let b = parse_pow(s, j + 1);
    term_loop(s, {i = b.i, e = mul(st.e, b.e)})
  )
  else if c == "/" then (
    let b = parse_pow(s, j + 1);
    term_loop(s, {i = b.i, e = divx(st.e, b.e)})
  )
  else st end end
)
let parse_term = fn s, i -> term_loop(s, parse_pow(s, i))

let expr_loop = fn s, st -> (
  let j = skipws(s, st.i);
  let c = peek(s, j);
  if c == "+" then (
    let b = parse_term(s, j + 1);
    expr_loop(s, {i = b.i, e = add(st.e, b.e)})
  )
  else if c == "-" then (
    let b = parse_term(s, j + 1);
    expr_loop(s, {i = b.i, e = sub(st.e, b.e)})
  )
  else st end end
)
let parse_expr = fn s, i -> expr_loop(s, parse_term(s, i))

let parse = fn src -> (
  let r = parse_expr(src, 1);
  let j = skipws(src, r.i);
  if j > length(src) then r.e else fail("trailing input at position " + str(j)) end
)

let deriv = fn src -> show(simp(ddx(parse(src))))
