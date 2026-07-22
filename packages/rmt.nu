% rmt.nu — random matrices, structured.
% Sugar over randn/qr/eye for the matrices you actually reach for:
% symmetric, SPD (chol-safe), orthogonal (Haar), permutations, correlation,
% row-stochastic, and the Gaussian orthogonal ensemble.
% Reproducible under rng(seed), like everything random.

% randsym(n): symmetric with N(0,1)-ish entries.
let randsym = fn n -> (
  let a = randn(n);
  (a + a') / 2)

% wishart(n): a' * a for a ~ N(0,1)^{n x n} — the raw Wishart ensemble.
% Positive definite almost surely, but can be ill-conditioned.
let wishart = fn n -> (
  let a = randn(n);
  a' * a)

% randspd(n): symmetric positive definite, chol-safe by construction
% (eigenvalues >= 1: wishart + identity).
let randspd = fn n -> wishart(n) + eye(n)

% randorth(n): Haar-distributed orthogonal matrix — QR of a Gaussian
% matrix with the sign fix (Q scaled by sign(diag(R))).
let randorth = fn n -> (
  let f = qr(randn(n));
  f.Q * diag(sign(diag(f.R))))

% randperm(n): a random permutation of 1:n, as ranks of uniform draws
% (each entry's rank among the draws; distinct almost surely).
let randperm = fn n -> (
  let r = rand(1, n);
  r ~> (fn x -> sum(r <= x)))

% permmat(p): the permutation matrix for p (rows of the identity, permuted).
let permmat = fn p -> (
  let n = numel(p);
  let I = eye(n);
  I[p, :])

% randcorr(n): a random correlation matrix — a Wishart, normalized to
% unit diagonal (D^-1/2 * S * D^-1/2).
let randcorr = fn n -> (
  let S = wishart(n);
  let d = diag(1 ./ sqrt(diag(S)));
  d * S * d)

% randstoch(n): row-stochastic (a random Markov transition matrix):
% uniform entries, rows normalized to sum 1.
let randstoch = fn n -> (
  let r = rand(n);
  r ./ repmat(sum(r, 2), 1, n))

% goe(n): the Gaussian orthogonal ensemble, scaled so the spectrum
% follows Wigner's semicircle on [-2, 2] as n grows.
let goe = fn n -> (
  let a = randn(n);
  (a + a') / sqrt(2 * n))
