# astro.nu — solar and lunar almanac for Neutrino
# Sunrise, sunset, dawn, dusk, solar noon, day length, sun position, and
# moon phase, for any latitude and longitude (degrees; east and north are
# positive). Times are local decimal hours given a UTC offset tz (e.g. -4
# for EDT). NOAA solar position algorithm; agrees with the astral reference
# library to about a minute. See tests/39_astro.test.

let astro_pi = 4 * atan(1)
let astro_sind = fn d -> sin(d * astro_pi / 180)
let astro_cosd = fn d -> cos(d * astro_pi / 180)
let astro_tand = fn d -> tan(d * astro_pi / 180)

# continuous Julian Day at 0h UT
let astro_jd = fn y, m, d -> (
  let a = floor((14 - m) / 12);
  let y2 = y + 4800 - a;
  let m2 = m + 12 * a - 3;
  d + floor((153 * m2 + 2) / 5) + 365 * y2
    + floor(y2 / 4) - floor(y2 / 100) + floor(y2 / 400) - 32045.5)

# solar declination (deg) and equation of time (minutes) at Julian centuries T
let astro_sun_core = fn T -> (
  let L0 = mod(280.46646 + T * (36000.76983 + 0.0003032 * T), 360);
  let M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
  let e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
  let C = astro_sind(M) * (1.914602 - T * (0.004817 + 0.000014 * T))
        + astro_sind(2 * M) * (0.019993 - 0.000101 * T)
        + astro_sind(3 * M) * 0.000289;
  let omega = 125.04 - 1934.136 * T;
  let lam = L0 + C - 0.00569 - 0.00478 * astro_sind(omega);
  let eps0 = 23 + (26 + (21.448 - T * (46.815 + T * (0.00059 - 0.001813 * T))) / 60) / 60;
  let eps = eps0 + 0.00256 * astro_cosd(omega);
  let decl = asin(astro_sind(eps) * astro_sind(lam)) * 180 / astro_pi;
  let y2 = astro_tand(eps / 2) ^ 2;
  let eqt = 4 * (180 / astro_pi) * (
      y2 * astro_sind(2 * L0) - 2 * e * astro_sind(M)
    + 4 * e * y2 * astro_sind(M) * astro_cosd(2 * L0)
    - 0.5 * y2 ^ 2 * astro_sind(4 * L0) - 1.25 * e ^ 2 * astro_sind(2 * M));
  {decl = decl, eqt = eqt})

# generic rise/set event at a given solar zenith angle (deg). rising: Bool.
let astro_event = fn lat, lon, y, m, d, tz, zenith, rising -> (
  assert((lat >= -90) && (lat <= 90), "latitude must be in [-90, 90], got {}", lat);
  assert((lon >= -180) && (lon <= 180), "longitude must be in [-180, 180], got {}", lon);
  let jd0 = astro_jd(y, m, d) + 0.5 - lon / 360;      % local solar noon, first guess
  let s = astro_sun_core((jd0 - 2451545) / 36525);
  let x = astro_cosd(zenith) / (astro_cosd(lat) * astro_cosd(s.decl))
        - astro_tand(lat) * astro_tand(s.decl);
  assert((x >= -1) && (x <= 1),
         "the sun does not cross {} degrees at latitude {} on {}-{}-{}", zenith, lat, y, m, d);
  let ha = acos(x) * 180 / astro_pi;
  let ha2 = if rising then ha else -ha end;
  let minutes = 720 - 4 * (lon + ha2) - s.eqt;
  % refine once: re-evaluate the sun at the event itself
  let jd1 = astro_jd(y, m, d) + minutes / 1440;
  let s2 = astro_sun_core((jd1 - 2451545) / 36525);
  let x2 = astro_cosd(zenith) / (astro_cosd(lat) * astro_cosd(s2.decl))
         - astro_tand(lat) * astro_tand(s2.decl);
  let ha3 = if rising then acos(x2) else -acos(x2) end * 180 / astro_pi;
  mod((720 - 4 * (lon + ha3) - s2.eqt) / 60 + tz, 24))

# ---- the almanac -----------------------------------------------------
let sunrise    = fn lat, lon, y, m, d, tz -> astro_event(lat, lon, y, m, d, tz, 90.833, true)
let sunset     = fn lat, lon, y, m, d, tz -> astro_event(lat, lon, y, m, d, tz, 90.833, false)
let dawn_civil = fn lat, lon, y, m, d, tz -> astro_event(lat, lon, y, m, d, tz, 96, true)
let dusk_civil = fn lat, lon, y, m, d, tz -> astro_event(lat, lon, y, m, d, tz, 96, false)
let dawn_nautical = fn lat, lon, y, m, d, tz -> astro_event(lat, lon, y, m, d, tz, 102, true)
let dusk_nautical = fn lat, lon, y, m, d, tz -> astro_event(lat, lon, y, m, d, tz, 102, false)
let dawn_astro = fn lat, lon, y, m, d, tz -> astro_event(lat, lon, y, m, d, tz, 108, true)
let dusk_astro = fn lat, lon, y, m, d, tz -> astro_event(lat, lon, y, m, d, tz, 108, false)

let solar_noon = fn lon, y, m, d, tz -> (
  let jd0 = astro_jd(y, m, d) + 0.5 - lon / 360;
  let s = astro_sun_core((jd0 - 2451545) / 36525);
  mod((720 - 4 * lon - s.eqt) / 60 + tz, 24))

let day_length = fn lat, lon, y, m, d -> (
  mod(sunset(lat, lon, y, m, d, 0) - sunrise(lat, lon, y, m, d, 0), 24))

# sun_position(lat, lon, y, m, d, hour, tz): altitude and azimuth (deg) at a
# local time. Azimuth is from north, clockwise.
let sun_position = fn lat, lon, y, m, d, hour, tz -> (
  let jd = astro_jd(y, m, d) + (hour - tz) / 24;
  let s = astro_sun_core((jd - 2451545) / 36525);
  let tst = mod(hour * 60 + s.eqt + 4 * lon - 60 * tz, 1440);
  let ha = tst / 4 - 180;
  let cosz = astro_sind(lat) * astro_sind(s.decl)
           + astro_cosd(lat) * astro_cosd(s.decl) * astro_cosd(ha);
  let zen = acos(cosz) * 180 / astro_pi;
  let caz = (astro_sind(lat) * cosz - astro_sind(s.decl)) / (astro_cosd(lat) * astro_sind(zen));
  let az0 = acos(max(-1, min(1, caz))) * 180 / astro_pi;
  {alt = 90 - zen, az = if ha > 0 then mod(az0 + 180, 360) else mod(540 - az0, 360) end})

# moon_age(y, m, d): days since new moon (0..29.53); moon_illum: fraction lit.
let moon_age = fn y, m, d -> mod(astro_jd(y, m, d) + 0.5 - 2451550.26, 29.530588853)
let moon_illum = fn y, m, d -> (1 - astro_cosd(360 * moon_age(y, m, d) / 29.530588853)) / 2

# hm(h): decimal hours -> "HH:MM" string.
let hm = fn h -> (
  let t = mod(h, 24);
  let hh = floor(t);
  let mm = round((t - hh) * 60);
  let hh2 = mod(if mm == 60 then hh + 1 else hh end, 24);
  let mm2 = if mm == 60 then 0 else mm end;
  (if hh2 < 10 then "0" else "" end) + fmt("{:.0f}", hh2) + ":"
    + (if mm2 < 10 then "0" else "" end) + fmt("{:.0f}", mm2))

# ---- preselected places (lat, lon; degrees, east/north positive) -----
let places = {
  alexandria_va = {lat = 38.8048, lon = -77.0469},
  duluth_ga     = {lat = 34.0029, lon = -84.1446},
  washington_dc = {lat = 38.9072, lon = -77.0369},
  ljubljana     = {lat = 46.0569, lon =  14.5058},
  new_york      = {lat = 40.7128, lon = -74.0060},
  london        = {lat = 51.5074, lon =  -0.1278},
  tokyo         = {lat = 35.6762, lon = 139.6503},
  san_francisco = {lat = 37.7749, lon = -122.4194}
}

# drive_daylight(from, to, y, m, d, tz): the daylight driving window — civil
# dawn at the origin to civil dusk at the destination, with sunrise/sunset
# at both ends. from/to are place records {lat, lon}.
let drive_daylight = fn from, to, y, m, d, tz -> (
  {depart_dawn  = dawn_civil(from.lat, from.lon, y, m, d, tz),
   depart_rise  = sunrise(from.lat, from.lon, y, m, d, tz),
   arrive_set   = sunset(to.lat, to.lon, y, m, d, tz),
   arrive_dusk  = dusk_civil(to.lat, to.lon, y, m, d, tz),
   window_hours = mod(dusk_civil(to.lat, to.lon, y, m, d, tz)
                    - dawn_civil(from.lat, from.lon, y, m, d, tz), 24)})
