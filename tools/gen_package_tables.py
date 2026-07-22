#!/usr/bin/env python3
"""Generate or verify the worked-example tables in PACKAGES.md.

Every package function's table row carries a worked example and its actual
result, produced by executing the example against ./vmtest. Modes:
    python3 tools/gen_package_tables.py           # regenerate tables in place
    python3 tools/gen_package_tables.py --check   # verify tables match reality
--check re-executes every example and fails on drift, so the worked examples
hold the same machine-verified status as the manual transcripts.
"""
import json, re, subprocess, sys

PLACES_EX = "places.duluth_ga.lat"
SPECS = {
  "dist": ("load(\"packages/dist.nu\"); format(6); rng(7)", [
    ("norm.pdf/cdf/inv(x, mu, sigma)", "norm.cdf(1.96, 0, 1)"),
    ("norm.rand(n, mu, sigma)",        "mean(norm.rand(500, 10, 2)) > 9"),
    ("student.pdf/cdf/inv(x, v)",      "student.inv(0.975, 30)"),
    ("chi2.pdf/cdf/inv(x, k)",         "chi2.inv(0.95, 3)"),
    ("fdist.pdf/cdf/inv(x, d1, d2)",   "fdist.cdf(3.0, 4, 20)"),
    ("expo.pdf/cdf/inv(x, rate)",      "expo.inv(0.5, 2)"),
    ("unif.pdf/cdf/inv(x, a, b)",      "unif.cdf(3, 0, 10)"),
  ]),
  "poly": ("load(\"packages/poly.nu\"); format(6)", [
    ("polyval(c, x)",    "polyval([2, -3, 1], 4)"),
    ("roots(c)",         "sort(real(roots([1, -6, 11, -6])))\x27"),
    ("companion(c)",     "companion([1, 0, -4])"),
    ("polyfit(x, y, n)", "polyfit([1, 2, 3], [2, 5, 10], 2)"),
    ("polyder(c)",       "polyder([1, -6, 11, -6])"),
    ("polyint(c, k)",    "polyint([3, -12, 11], -6)"),
    ("conv(a, b)",       "conv([1, -1], [1, -2])"),
  ]),
  "finance": ("load(\"packages/finance.nu\"); format(6)", [
    ("pmt(n, i, pv, fv)",    "pmt(360, 0.005, 250000, 0)"),
    ("pv(n, i, pmt, fv)",    "pv(360, 0.005, -1498.88, 0)"),
    ("fv(n, i, pv, pmt)",    "fv(120, 0.004, 0, -200)"),
    ("nper(i, pv, pmt, fv)", "nper(0.005, 250000, -1498.88, 0)"),
    ("rate(n, pv, pmt, fv)", "rate(360, 250000, -1498.876313, 0)"),
    ("npv(r, cfs)",          "npv(0.10, [-1000, 300, 420, 680])"),
    ("irr(cfs)",             "irr([-1000, 300, 420, 680])"),
    ("bond_price(face, crate, y, n, freq)",    "bond_price(100, 0.08, 0.06, 10, 1)"),
    ("bond_ytm(price, face, crate, n, freq)",  "bond_ytm(114.7202, 100, 0.08, 10, 1)"),
    ("bond_duration(face, crate, y, n, freq)", "bond_duration(100, 0.08, 0.06, 10, 1)"),
    ("bond_mduration(face, crate, y, n, freq)","bond_mduration(100, 0.08, 0.06, 10, 1)"),
    ("bond_convexity(face, crate, y, n, freq)","bond_convexity(100, 0.08, 0.06, 10, 1)"),
    ("amort(principal, i, n)", "amort(1000, 0.01, 3).balance[3]"),
    ("datenum(y, m, d)",   "datenum(2026, 7, 17)"),
    ("datestr(jdn)",       "datestr(datenum(2026, 7, 17) + 90)"),
    ("daterec(jdn)",       "daterec(datenum(2026, 7, 17))"),
    ("dateadd(y, m, d, k)","dateadd(2024, 12, 31, 1)"),
    ("today()",            "today() == datenum(now().y, now().m, now().d)"),
    ("dow(y, m, d)",       "dow(2026, 7, 17)"),
    ("days(y1,m1,d1, y2,m2,d2)",    "days(2026, 1, 1, 2026, 7, 17)"),
    ("days360(y1,m1,d1, y2,m2,d2)", "days360(2024, 1, 31, 2024, 7, 31)"),
  ]),
  "astro": ("load(\"packages/astro.nu\"); format(6)", [
    ("sunrise(lat, lon, y, m, d, tz)", "hm(sunrise(38.8048, -77.0469, 2026, 7, 17, -4))"),
    ("sunset(lat, lon, y, m, d, tz)",  "hm(sunset(38.8048, -77.0469, 2026, 7, 17, -4))"),
    ("dawn_civil / dusk_civil(lat, lon, y, m, d, tz)", "hm(dawn_civil(38.8048, -77.0469, 2026, 7, 17, -4))"),
    ("dawn_nautical / dusk_nautical(lat, lon, y, m, d, tz)", "hm(dawn_nautical(38.8048, -77.0469, 2026, 7, 17, -4))"),
    ("dawn_astro / dusk_astro(lat, lon, y, m, d, tz)", "hm(dusk_astro(38.8048, -77.0469, 2026, 7, 17, -4))"),
    ("solar_noon(lon, y, m, d, tz)",   "hm(solar_noon(-77.0469, 2026, 7, 17, -4))"),
    ("day_length(lat, lon, y, m, d)",  "day_length(46.0569, 14.5058, 2026, 7, 17)"),
    ("sun_position(lat, lon, y, m, d, hour, tz)", "sun_position(38.8048, -77.0469, 2026, 7, 17, 13.2, -4).alt"),
    ("moon_age(y, m, d)",   "moon_age(2026, 7, 17)"),
    ("moon_illum(y, m, d)", "moon_illum(2026, 7, 17)"),
    ("hm(h)",               "hm(6.05)"),
    ("places",              PLACES_EX),
    ("drive_daylight(from, to, y, m, d, tz)", "drive_daylight(places.alexandria_va, places.duluth_ga, 2026, 7, 17, -4).window_hours"),
  ]),
  "rmt": ("load(\"packages/rmt.nu\"); format(6); rng(7)", [
    ("randsym(n)",   "let S = randsym(3); max(max(abs(S - S\x27))) == 0"),
    ("randspd(n)",   "let P = randspd(3); min(eig(P).values) >= 1"),
    ("wishart(n)",   "let W = wishart(3); min(eig(W).values) > 0"),
    ("randorth(n)",  "let Q = randorth(3); max(max(abs(Q\x27 * Q - eye(3)))) < 1e-12"),
    ("randperm(n)",  "sort(randperm(5)) == 1:5"),
    ("permmat(p)",   "permmat([2, 3, 1])"),
    ("randcorr(n)",  "let C = randcorr(3); max(abs(diag(C) - 1)) < 1e-12"),
    ("randstoch(n)", "let T = randstoch(4); max(abs(sum(T, 2) - 1)) < 1e-12"),
    ("goe(n)",       "let H = goe(150); max(abs(eig(H).values)) < 2.4"),
  ]),

}

def esc(t): return t.replace("|", "\\|")

def run_examples(load, rows):
    prog = load + "\n" + "".join(ex + "\n\"@@S@@\"\n" for _, ex in rows)
    r = subprocess.run(["./vmtest"], input=prog, capture_output=True, text=True, timeout=60)
    parts = r.stdout.split("\"@@S@@\"")
    out = []
    for k, (sig, ex) in enumerate(rows):
        seg = parts[k].strip("\n").split("\n")[-1] if k < len(parts) and parts[k].strip("\n") else ""
        out.append((sig, ex, seg))
    return out

def render(rows):
    lines = ["| Function | Worked example | Result |", "|---|---|---|"]
    for sig, ex, res in rows:
        lines.append(f"| `{esc(sig)}` | `{esc(ex)}` | `{esc(res)}` |")
    return "\n".join(lines)

def main():
    check = "--check" in sys.argv
    s = open("PACKAGES.md").read()
    blocks = re.findall(r"\| Function \| Worked example \| Result \|\n\|---\|---\|---\|\n(?:\|[^\n]*\n)+", s)
    if len(blocks) != len(SPECS):
        raise SystemExit(f"gen_package_tables: expected {len(SPECS)} tables, found {len(blocks)}")
    changed = False
    for old, (name, (load, rows)) in zip(blocks, SPECS.items()):
        newt = render(run_examples(load, rows)) + "\n"
        empties = newt.count("| ``")
        if empties:
            raise SystemExit(f"gen_package_tables: {name} has {empties} empty result(s)")
        if old != newt:
            changed = True
            if check:
                print(f"package tables: DRIFT in {name}")
            else:
                s = s.replace(old, newt)
    if check:
        if changed: sys.exit(1)
        print(f"package tables: all worked examples verified against the interpreter")
    else:
        open("PACKAGES.md", "w").write(s)
        print("package tables: regenerated" if changed else "package tables: already current")

if __name__ == "__main__":
    main()

