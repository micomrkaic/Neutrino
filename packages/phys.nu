% phys.nu — physical constants (CODATA 2018, SI units).
% Since the 2019 SI redefinition, c, h, k, NA, and the elementary charge are
% exact by definition; the rest are the CODATA recommended values.
% Usage: load("packages/phys.nu");  phys.c, phys.hbar, phys.G, ...

let phys = {
  c     = 299792458,            % speed of light in vacuum, m/s (exact)
  h     = 6.62607015e-34,       % Planck constant, J s (exact)
  hbar  = 6.62607015e-34 / (2 * pi),  % reduced Planck constant, J s
  G     = 6.67430e-11,          % Newtonian gravitation, m^3 kg^-1 s^-2
  g     = 9.80665,              % standard gravity, m/s^2 (exact by convention)
  k     = 1.380649e-23,         % Boltzmann constant, J/K (exact)
  NA    = 6.02214076e23,        % Avogadro constant, 1/mol (exact)
  R     = 1.380649e-23 * 6.02214076e23,  % molar gas constant, J mol^-1 K^-1
  qe    = 1.602176634e-19,      % elementary charge, C (exact)
  me    = 9.1093837015e-31,     % electron mass, kg
  mp    = 1.67262192369e-27,    % proton mass, kg
  eps0  = 8.8541878128e-12,     % vacuum permittivity, F/m
  mu0   = 1.25663706212e-6,     % vacuum permeability, N/A^2
  alpha = 7.2973525693e-3,      % fine-structure constant (dimensionless)
  sigma = 5.670374419e-8,       % Stefan-Boltzmann, W m^-2 K^-4
  eV    = 1.602176634e-19,      % electron volt, J (exact)
  au    = 1.495978707e11,       % astronomical unit, m (exact)
  ly    = 9460730472580800      % light year, m (exact: c * Julian year)
}
