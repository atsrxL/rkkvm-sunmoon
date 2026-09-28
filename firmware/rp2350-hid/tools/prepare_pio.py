#!/usr/bin/env python3
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
source, target = map(Path, sys.argv[1:])
expected = {'pio_usb_device.c': '0fa0ab6f14695594ddff5c1cd8381ac71b02c1dfa46ceb2906e723676f200b3e', 'pio_usb.c': '02bb9c9a7189ecb41e4b4257dab3b145a4d7f8cb15784560dbbeb609fd660c87'}
commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
if commit != '3c1eec341a5232640e4c00628b889b641af34b28':
    raise SystemExit('PIO-USB commit mismatch')
for name, digest in expected.items():
    if hashlib.sha256((source/'src'/name).read_bytes()).hexdigest() != digest:
        raise SystemExit('PIO-USB source mismatch: '+name)
if target.exists():
    shutil.rmtree(target)
shutil.copytree(source, target, ignore=shutil.ignore_patterns('.git'))
patch = Path(__file__).resolve().parent.parent/'patches/pio-device.patch'
subprocess.run(['git', 'apply', '--check', str(patch)], cwd=target, check=True)
subprocess.run(['git', 'apply', str(patch)], cwd=target, check=True)
