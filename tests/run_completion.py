#!/usr/bin/env python3
"""Tab-completion smoke test: drives ./neutrino in a pty. Skips (exit 0 with a
note) when no pty or readline support is available, so CI stays portable."""
import os, pty, re, signal, sys, time

if not os.path.exists('./neutrino'):
    print("completion: ./neutrino not built, skipping"); sys.exit(0)
# a readline-less build has no completion to test
import subprocess
if b'rl_completion' not in subprocess.run(['strings', './neutrino'], capture_output=True).stdout and \
   subprocess.run(['sh', '-c', 'ldd ./neutrino 2>/dev/null | grep -q readline'], capture_output=True).returncode != 0:
    print("completion: built without readline, skipping"); sys.exit(0)

def drive(keys):
    pid, fd = pty.fork()
    if pid == 0:
        os.execv('./neutrino', ['./neutrino'])
    time.sleep(0.6)
    try: os.read(fd, 65536)
    except OSError: pass
    os.write(fd, keys.encode())
    time.sleep(0.5)
    out = b''
    try: out = os.read(fd, 65536)
    except OSError: pass
    os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0); os.close(fd)
    return re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', out.decode(errors='replace'))

try:
    checks = [
        ('sqr\t',                 'sqrt',        'builtin name'),
        ('load("tests/da\t',      'tests/data/', 'file path inside quotes'),
    ]
    fails = 0
    for keys, want, what in checks:
        got = drive(keys)
        if want not in got:
            print(f"completion FAIL ({what}): sent {keys!r}, wanted {want!r}, got {got[:60]!r}")
            fails += 1
    if fails: sys.exit(1)
    print("completion: builtin + file-path completion OK")
except OSError as e:
    print(f"completion: no usable pty ({e}), skipping")
