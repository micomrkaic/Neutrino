% scatter.nu — scatter plots as a pure package (no core changes).
% The frozen plot() already honors style = "points" in every backend
% (SVG circles in the browser, lone markers in ascii, points in gnuplot);
% these are conveniences over that fact.
%
% Honest scope: per-point sizes/colors (bubble charts) and mixed-style
% overlays would require backend changes, so they are deliberately absent.
% For full option control call plot(x, y, {style = "points", ...}) directly.

let scatter = fn x, y -> plot(x, y, {style = "points"})

let scatter_titled = fn x, y, t -> plot(x, y, {style = "points", title = t})

% jitter(x, amount): spread overplotted values by uniform noise in
% [-amount/2, amount/2], preserving shape. Seed rng() first for
% reproducibility.
let jitter = fn x, amount -> (
  let s = size(x);
  x + (rand(s[1], s[2]) - 0.5) * amount
)
