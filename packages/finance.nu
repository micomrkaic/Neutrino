# finance.nu — the HP-12C's greatest hits, for Neutrino
# Time value of money, cash flows, bonds, amortization, and date arithmetic.
# Sign convention (HP-12C): money you receive is positive, money you pay is
# negative. All rates are per period as decimals (0.005 = 0.5%/month).
# Cross-checked against independent references; see tests/38_finance.test.

# ---- time value of money (END mode) --------------------------------
# The TVM identity: pv*(1+i)^n + pmt*((1+i)^n - 1)/i + fv = 0

let fin_check_i = fn i -> assert(i > -1, "rate must be > -1, got {}", i)

# pmt(n, i, pv, fv): the level payment.
let pmt = fn n, i, pv, fv -> (
  fin_check_i(i);
  assert(n > 0, "pmt: n must be positive, got {}", n);
  if i == 0 then -(pv + fv) / n
  else -(pv * (1 + i)^n + fv) * i / ((1 + i)^n - 1) end)

# fv(n, i, pv, pmt): future value after n periods.
let fv = fn n, i, pv, p -> (
  fin_check_i(i);
  if i == 0 then -(pv + p * n)
  else -(pv * (1 + i)^n + p * ((1 + i)^n - 1) / i) end)

# pv(n, i, pmt, fv): present value of payments plus a future amount.
let pv = fn n, i, p, f -> (
  fin_check_i(i);
  if i == 0 then -(f + p * n)
  else -(f + p * ((1 + i)^n - 1) / i) / (1 + i)^n end)

# nper(i, pv, pmt, fv): number of periods, solved directly.
let nper = fn i, pv0, p, f -> (
  fin_check_i(i);
  if i == 0 then -(pv0 + f) / p
  else ln((p - f * i) / (p + pv0 * i)) / ln(1 + i) end)

# rate(n, pv, pmt, fv): the per-period rate, solved numerically. The upper
# bracket grows adaptively so large n cannot overflow the probe.
let fin_rate_hi = fn g, lo, hi -> (
  if isfinite(g(hi)) && (g(hi) * g(lo) > 0) then fin_rate_hi(g, lo, hi * 2) else hi end)
let rate = fn n, pv0, p, f -> (
  let g = fn i -> pv0 * (1 + i)^n + p * ((1 + i)^n - 1) / i + f;
  let hi = fin_rate_hi(g, 1e-9, 0.01);
  assert(isfinite(g(hi)), "rate: no sign change found (check the signs of pv/pmt/fv)");
  fzero(g, 1e-9, hi))

# ---- cash flows ------------------------------------------------------
# Cash flows as a vector, first entry at t = 0 (the HP-12C's CF0).

let npv = fn r, cfs -> (
  fin_check_i(r);
  sum(cfs ./ (1 + r) .^ (0 : numel(cfs) - 1)))

let irr = fn cfs -> (
  assert(numel(cfs) >= 2, "irr: need at least two cash flows");
  assert((min(cfs) < 0) && (max(cfs) > 0),
         "irr: cash flows must change sign (some inflow and some outflow)");
  fzero(fn r -> npv(r, cfs), -0.9999, 100))

# ---- bonds -----------------------------------------------------------
# Period-based (n periods to maturity, freq coupons per year). Rates annual.

let bond_price = fn face, crate, y, n, freq -> (
  assert(n > 0, "bond_price: n must be positive");
  let c = face * crate / freq;
  let k = 1 : n * freq;
  sum(c ./ (1 + y / freq) .^ k) + face / (1 + y / freq)^(n * freq))

let bond_ytm = fn price, face, crate, n, freq -> (
  assert(price > 0, "bond_ytm: price must be positive");
  fzero(fn y -> bond_price(face, crate, y, n, freq) - price, 1e-9, 10))

let bond_duration = fn face, crate, y, n, freq -> (
  let c = face * crate / freq;
  let k = 1 : n * freq;
  let disc = c ./ (1 + y / freq) .^ k;
  let P = sum(disc) + face / (1 + y / freq)^(n * freq);
  (sum((k ./ freq) .* disc) + (n * freq / freq) * face / (1 + y / freq)^(n * freq)) / P)

let bond_mduration = fn face, crate, y, n, freq -> (
  bond_duration(face, crate, y, n, freq) / (1 + y / freq))

let bond_convexity = fn face, crate, y, n, freq -> (
  let c = face * crate / freq;
  let k = 1 : n * freq;
  let cf = 0 .* k + c;
  cf[n * freq] = c + face;
  let disc = cf ./ (1 + y / freq) .^ k;
  sum((k .* (k + 1)) .* disc) / (sum(disc) * (1 + y / freq)^2 * freq^2))

# ---- amortization ----------------------------------------------------
# amort(principal, i, n): the full schedule as a record of column vectors —
# {period, payment, interest, principal, balance}. Plot the balance!

let amort = fn principal, i, n -> (
  let p = -pmt(n, i, principal, 0);
  let period = reshape(1:n, n, 1);
  let payment = 0 .* period + p;
  let interest = 0 .* period;
  let princ = 0 .* period;
  let balance = 0 .* period;
  let bal = principal;
  for k = 1:n do
    interest[k] = bal * i;
    princ[k] = p - interest[k];
    bal = bal - princ[k];
    balance[k] = bal
  end;
  {period = period, payment = payment, interest = interest,
   principal = princ, balance = balance})

# ---- dates (HP-12C's date arithmetic) --------------------------------
# Serial dates via the Julian Day Number; y, m, d as integers.

let datenum = fn y, m, d -> (
  assert((m >= 1) && (m <= 12), "datenum: month must be 1..12, got {}", m);
  assert((d >= 1) && (d <= 31), "datenum: day must be 1..31, got {}", d);
  let a = floor((14 - m) / 12);
  let y2 = y + 4800 - a;
  let m2 = m + 12 * a - 3;
  d + floor((153 * m2 + 2) / 5) + 365 * y2
    + floor(y2 / 4) - floor(y2 / 100) + floor(y2 / 400) - 32045)

let datestr = fn jdn -> (
  let a = jdn + 32044;
  let b = floor((4 * a + 3) / 146097);
  let c = a - floor(146097 * b / 4);
  let d2 = floor((4 * c + 3) / 1461);
  let e = c - floor(1461 * d2 / 4);
  let m2 = floor((5 * e + 2) / 153);
  {y = 100 * b + d2 - 4800 + floor(m2 / 10),
   m = m2 + 3 - 12 * floor(m2 / 10),
   d = e - floor((153 * m2 + 2) / 5) + 1})

# days(y1,m1,d1, y2,m2,d2): actual days between two dates (HP-12C ΔDYS).
let days = fn y1, m1, d1, y2, m2, d2 -> datenum(y2, m2, d2) - datenum(y1, m1, d1)

# dateadd(y,m,d, k): the calendar date k days later (HP-12C DATE).
let dateadd = fn y, m, d, k -> datestr(datenum(y, m, d) + k)

# dow(y,m,d): day of week, 1 = Monday .. 7 = Sunday.
let dow = fn y, m, d -> mod(datenum(y, m, d), 7) + 1

# days360(y1,m1,d1, y2,m2,d2): 30/360 US day count (bond conventions).
let days360 = fn y1, m1, d1, y2, m2, d2 -> (
  let dd1 = min(d1, 30);
  let dd2 = if (d2 == 31) && (dd1 == 30) then 30 else d2 end;
  (y2 - y1) * 360 + (m2 - m1) * 30 + (dd2 - dd1))
