# poly.nu — polynomials for Neutrino
# Coefficients are row vectors, highest power first (Octave convention):
# [2, -3, 1] is 2x^2 - 3x + 1. Cross-checked against NumPy; see tests/37_poly.test.

# companion(c): the companion matrix; its eigenvalues are the roots of c.
let companion = fn c -> (
  assert(numel(c) >= 2, "companion: need a polynomial of degree >= 1");
  assert(c[1] != 0, "companion: leading coefficient must be nonzero");
  let n = numel(c) - 1;
  let a = c[2 : n + 1] ./ c[1];
  let C = zeros(n, n);
  C[1, :] = -a;
  for i = 2:n do
    C[i, i - 1] = 1
  end;
  C)

# roots(c): all complex roots, via the companion matrix and eig.
let roots = fn c -> eig(companion(c)).values

# polyval(c, x): evaluate at x (scalar or array, elementwise), Horner's rule.
let polyval = fn c, x -> (
  let acc = 0 .* x;                   % zeros shaped like x (works for scalars too)
  for i = 1 : numel(c) do
    acc = acc .* x + c[i]
  end;
  acc)

# polyfit(x, y, n): least-squares polynomial of degree n through (x, y).
# Vandermonde matrix + backslash (QR least squares for tall systems).
let polyfit = fn x, y, n -> (
  assert(numel(x) == numel(y), "polyfit: x and y must have the same length, got {} and {}", numel(x), numel(y));
  assert(numel(x) >= n + 1, "polyfit: need at least {} points for degree {}", n + 1, n);
  let m = numel(x);
  let V = zeros(m, n + 1);
  for j = 1 : n + 1 do
    V[:, j] = reshape(x, m, 1) .^ (n - j + 1)
  end;
  (V \ reshape(y, m, 1))')

# polyder(c): coefficients of the derivative.
let polyder = fn c -> (
  let n = numel(c) - 1;
  if n == 0 then [0] else c[1:n] .* (n : -1 : 1) end)

# polyint(c, k): coefficients of the antiderivative, with constant term k.
let polyint = fn c, k -> (
  let n = numel(c);
  [c ./ (n : -1 : 1), k])

# conv(a, b): polynomial multiplication (coefficient convolution).
let conv = fn a, b -> (
  let na = numel(a);
  let nb = numel(b);
  let out = zeros(1, na + nb - 1);
  for i = 1:na do
    for j = 1:nb do
      out[i + j - 1] = out[i + j - 1] + a[i] * b[j]
    end
  end;
  out)
