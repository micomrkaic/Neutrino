# dist.nu — probability distributions for Neutrino
# pdf / cdf / inv (quantile) / rand for: norm, student (t), chi2, fdist (F),
# expo (exponential), unif (uniform). Cross-checked against SciPy; see
# tests/34_dist.test. Quantile domain is 0 < p < 1.
#
# Conventions: parameters are explicit (no defaults) — norm.cdf(x, mu, sigma).
# Inverse CDFs with no closed form use bracket expansion + fzero on the CDF.

let dist_pi = 4 * atan(1)

# ---- generic quantile machinery: expand a bracket, then fzero ----
let dist_grow_hi = fn c, p, hi -> if c(hi) >= p then hi else dist_grow_hi(c, p, 2 * hi) end
let dist_grow_lo = fn c, p, lo -> if c(lo) <= p then lo else dist_grow_lo(c, p, 2 * lo) end
let dist_inv_pos = fn c, p -> fzero(fn x -> c(x) - p, 0, dist_grow_hi(c, p, 1))
let dist_inv_sym = fn c, p -> fzero(fn x -> c(x) - p, dist_grow_lo(c, p, -1), dist_grow_hi(c, p, 1))

# ---- standard normal core ----
let dist_phi  = fn z -> exp(-z^2 / 2) / sqrt(2 * dist_pi)
let dist_Phi  = fn z -> (1 + erf(z / sqrt(2))) / 2

# ---- normal(mu, sigma) ----
let dist_norm_pdf  = fn x, mu, sigma -> dist_phi((x - mu) / sigma) / sigma
let dist_norm_cdf  = fn x, mu, sigma -> dist_Phi((x - mu) / sigma)
let dist_norm_inv  = fn p, mu, sigma -> mu + sigma * norminv(p)
let dist_norm_rand = fn n, mu, sigma -> mu + sigma * randn(n, 1)
let norm = {pdf = dist_norm_pdf, cdf = dist_norm_cdf, inv = dist_norm_inv, rand = dist_norm_rand}

# ---- Student t(v) ----
let dist_t_pdf = fn x, v -> exp(lgamma((v + 1) / 2) - lgamma(v / 2)) / sqrt(v * dist_pi) * (1 + x^2 / v) ^ (-(v + 1) / 2)
let dist_t_cdf = fn x, v -> (
let i = betainc(v / (v + x^2), v / 2, 0.5); if x > 0 then 1 - i / 2 else i / 2 end)
let dist_t_inv  = fn p, v -> dist_inv_sym(fn x -> dist_t_cdf(x, v), p)
let dist_t_rand = fn n, v -> map(fn u -> dist_t_inv(u, v), rand(n, 1))
let student = {pdf = dist_t_pdf, cdf = dist_t_cdf, inv = dist_t_inv, rand = dist_t_rand}

# ---- chi-squared(k) ----
let dist_chi2_pdf = fn x, k -> if x <= 0 then 0 else exp((k / 2 - 1) * ln(x) - x / 2 - lgamma(k / 2) - (k / 2) * ln(2)) end
let dist_chi2_cdf  = fn x, k -> if x <= 0 then 0 else gammainc(x / 2, k / 2) end
let dist_chi2_inv  = fn p, k -> dist_inv_pos(fn x -> dist_chi2_cdf(x, k), p)
let dist_chi2_rand = fn n, k -> map(fn u -> dist_chi2_inv(u, k), rand(n, 1))
let chi2 = {pdf = dist_chi2_pdf, cdf = dist_chi2_cdf, inv = dist_chi2_inv, rand = dist_chi2_rand}

# ---- F(d1, d2) ----
let dist_f_pdf = fn x, d1, d2 -> if x <= 0 then 0 else exp((d1 / 2) * ln(d1) + (d2 / 2) * ln(d2) + (d1 / 2 - 1) * ln(x) - ((d1 + d2) / 2) * ln(d2 + d1 * x) - lbeta(d1 / 2, d2 / 2)) end
let dist_f_cdf  = fn x, d1, d2 -> if x <= 0 then 0 else betainc(d1 * x / (d1 * x + d2), d1 / 2, d2 / 2) end
let dist_f_inv  = fn p, d1, d2 -> dist_inv_pos(fn x -> dist_f_cdf(x, d1, d2), p)
let dist_f_rand = fn n, d1, d2 -> map(fn u -> dist_f_inv(u, d1, d2), rand(n, 1))
let fdist = {pdf = dist_f_pdf, cdf = dist_f_cdf, inv = dist_f_inv, rand = dist_f_rand}

# ---- exponential(rate) ----
let dist_expo_pdf  = fn x, rate -> if x < 0 then 0 else rate * exp(-rate * x) end
let dist_expo_cdf  = fn x, rate -> if x < 0 then 0 else 1 - exp(-rate * x) end
let dist_expo_inv  = fn p, rate -> -ln(1 - p) / rate
let dist_expo_rand = fn n, rate -> map(fn u -> dist_expo_inv(u, rate), rand(n, 1))
let expo = {pdf = dist_expo_pdf, cdf = dist_expo_cdf, inv = dist_expo_inv, rand = dist_expo_rand}

# ---- uniform(a, b) ----
let dist_unif_pdf  = fn x, a, b -> if (x >= a) && (x <= b) then 1 / (b - a) else 0 end
let dist_unif_cdf  = fn x, a, b -> if x < a then 0 else (if x > b then 1 else (x - a) / (b - a) end) end
let dist_unif_inv  = fn p, a, b -> a + p * (b - a)
let dist_unif_rand = fn n, a, b -> a + (b - a) * rand(n, 1)
let unif = {pdf = dist_unif_pdf, cdf = dist_unif_cdf, inv = dist_unif_inv, rand = dist_unif_rand}

