#!/usr/bin/env bash
# The install story, end to end: `make install` into a scratch PREFIX,
# then the INSTALLED binary (no packages/ beside it) must find packages
# via the XDG data dir, from an unrelated directory. The man page's
# structure gates everywhere; its render needs groff and may skip, loudly.
set -u
cd "$(dirname "$0")/.."
fail=0
grep -q '^\.TH NEUTRINO 1' neutrino.1 || { echo "FAIL install: neutrino.1 missing or malformed"; fail=1; }
if command -v groff >/dev/null 2>&1; then
  groff -man -Tutf8 neutrino.1 > /tmp/nu_man.txt 2>/tmp/nu_man.err || { echo "FAIL install: man page does not render"; fail=1; }
  [ -s /tmp/nu_man.err ] && { echo "FAIL install: groff warnings:"; cat /tmp/nu_man.err; fail=1; }
  grep -q 'packeur' /tmp/nu_man.txt || { echo "FAIL install: man page lost its content"; fail=1; }
else
  echo "install: groff not installed, man render skipped"
fi
P=$(mktemp -d)
make install PREFIX="$P" >/dev/null || { echo "FAIL install: make install"; fail=1; }
[ -x "$P/bin/neutrino" ] || { echo "FAIL install: no binary"; fail=1; }
[ -f "$P/share/neutrino/packages/dist.nu" ] || { echo "FAIL install: packages missing"; fail=1; }
[ -f "$P/share/man/man1/neutrino.1" ] || { echo "FAIL install: man page missing"; fail=1; }
out=$(cd / && printf 'load("dist"); norm.cdf(1.96, 0, 1)\nlength(ls("packages"))\n' | XDG_DATA_HOME="$P/share" "$P/bin/neutrino" 2>&1)
echo "$out" | grep -q '0.975002' || { echo "FAIL install: installed binary cannot load packages"; fail=1; }
echo "$out" | grep -q '^9$' || { echo "FAIL install: installed ls(\"packages\") wrong"; fail=1; }
make uninstall PREFIX="$P" >/dev/null || { echo "FAIL install: make uninstall"; fail=1; }
[ -e "$P/bin/neutrino" ] && { echo "FAIL install: uninstall left the binary"; fail=1; }
[ -e "$P/share/neutrino" ] && { echo "FAIL install: uninstall left the data dir"; fail=1; }
rm -rf "$P"
[ $fail -eq 0 ] && echo "install: binary, packages, man page in and out clean"
exit $fail
