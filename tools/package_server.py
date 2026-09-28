#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble a self-contained RKMoon server release (directory + .tar.gz).

Inputs are built artifacts (docs/BUILD.md): the dedicated Sunshine binary, the MPP worker,
librockchip_mpp, the private runtime-library directory and the HDMI-RX codec module.
Repository files (Python HID bridge, supervisor, systemd units, EDID) are copied from this
checkout. The result installs with: sudo python3 tools/install_server.py --release DIR.
"""
import argparse, hashlib, json, os, shutil, subprocess, tarfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ['run.py', 'child_exec.py', 'systemd_release.py', 'hdmirx_audio_bind.py', 'edid_apply.py', 'install_server.py', 'capture_pool.py']
UNITS = ['rkmoon.service', 'rkmoon-edid.service', 'rkmoon-hdmirx-audio.service', 'rkmoon-capture-pool.service']
CONFIG = ['rkmoon-edid-eight-modes.bin', 'edid-requested-modes.json']


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def commit():
    try:
        rev = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', '--short=12', 'HEAD'], text=True).strip()
        dirty = subprocess.run(['git', '-C', str(ROOT), 'diff', '--quiet', 'HEAD', '--', 'python', 'tools', 'systemd', 'config', 'sunshine', 'src', 'include'])
        return rev + ('-dirty' if dirty.returncode else '')
    except (OSError, subprocess.CalledProcessError):
        return 'unknown'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--server', required=True, type=Path, help='built rkmoon-kvm')
    p.add_argument('--assets', required=True, type=Path, help='directory with apps.json and box.png')
    p.add_argument('--worker', required=True, type=Path, help='built rkmoon-worker')
    p.add_argument('--mpp-lib', required=True, type=Path, help='librockchip_mpp.so.* (installed as .so.1)')
    p.add_argument('--runtime-libs', required=True, type=Path, help='directory of private shared libraries for rkmoon-kvm')
    p.add_argument('--codec-ko', required=True, type=Path, help='rkmoon_hdmirx_codec.ko (kernel/hdmirx-codec/README.md)')
    p.add_argument('--licenses', type=Path, help='optional directory of license files to ship')
    p.add_argument('--version', help='release name; default <date>-<commit>')
    p.add_argument('--output', required=True, type=Path, help='parent directory for the release')
    a = p.parse_args()

    version = a.version or time.strftime('%Y%m%d-%H%M') + '-' + commit()
    out = a.output / ('rkmoon-server-' + version)
    if out.exists():
        raise SystemExit(f'{out} exists; choose another --version')
    for d in ['bin/assets', 'lib', 'tools', 'config', 'systemd', 'kernel', 'licenses']:
        (out / d).mkdir(parents=True)

    shutil.copy2(a.server, out / 'bin/rkmoon-kvm')
    shutil.copy2(a.worker, out / 'bin/rkmoon-worker')
    for name in ['apps.json', 'box.png']:
        shutil.copy2(a.assets / name, out / 'bin/assets' / name)
    for lib in sorted(a.runtime_libs.iterdir()):
        if '.so' in lib.name and lib.is_file():
            shutil.copy2(lib, out / 'lib' / lib.name, follow_symlinks=True)
    shutil.copy2(a.mpp_lib, out / 'lib/librockchip_mpp.so.1', follow_symlinks=True)
    shutil.copy2(a.codec_ko, out / 'kernel/rkmoon_hdmirx_codec.ko')
    shutil.copytree(ROOT / 'python/rkmoon_hid', out / 'python/rkmoon_hid',
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    for name in TOOLS:
        shutil.copy2(ROOT / 'tools' / name, out / 'tools' / name)
    for name in UNITS:
        shutil.copy2(ROOT / 'systemd' / name, out / 'systemd' / name)
    for name in CONFIG:
        shutil.copy2(ROOT / 'config' / name, out / 'config' / name)
    shutil.copy2(ROOT / 'LICENSE', out / 'licenses/RKMoon-LICENSE') if (ROOT / 'LICENSE').exists() else None
    if a.licenses:
        for f in a.licenses.iterdir():
            if f.is_file():
                shutil.copy2(f, out / 'licenses' / f.name)
    for f in out.rglob('*'):
        if f.is_file():
            f.chmod(0o755 if f.parent.name in {'bin', 'tools'} else 0o644)

    files = {str(f.relative_to(out)): sha(f) for f in sorted(out.rglob('*')) if f.is_file()}
    manifest = {'version': version, 'source_commit': commit(), 'files': files}
    (out / 'MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
    archive = out.with_name(out.name + '.tar.gz')
    with tarfile.open(archive, 'w:gz') as t:
        t.add(out, arcname=out.name)
    print(out)
    print(archive, sha(archive))


if __name__ == '__main__':
    main()
