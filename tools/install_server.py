#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install an RKMoon server release as boot-time systemd services (run as root).

  sudo python3 tools/install_server.py --release rkmoon-server-VERSION[.tar.gz] --user at [--start]

Copies the release to /opt/rkmoon/releases/VERSION and points /opt/rkmoon/current at it.
First install also writes /etc/rkmoon/runtime.json and enables rkmoon-hdmirx-audio,
rkmoon-edid and rkmoon. Existing configuration and state are kept, so an update only
switches the symlink and restarts rkmoon.service. Roll back with --activate OLD_VERSION.
The USB input backend (rkmoon-input) is installed separately by tools/install_input.py.
"""
import argparse, grp, hashlib, json, os, pwd, shutil, subprocess, sys, tarfile, tempfile
from pathlib import Path

PREFIX = Path('/opt/rkmoon')
CONFIG = Path('/etc/rkmoon/runtime.json')
STATE = Path('/var/lib/rkmoon')
UNITS = Path('/etc/systemd/system')
ORDER = ['rkmoon-hdmirx-audio.service', 'rkmoon-edid.service', 'rkmoon-capture-pool.service', 'rkmoon.service']


def run(*args, check=True):
    return subprocess.run(args, check=check)


def verify(release):
    manifest = json.loads((release / 'MANIFEST.json').read_text())
    for name, digest in manifest['files'].items():
        if hashlib.sha256((release / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f'checksum mismatch: {name}')
    return manifest['version']


def unpack(source, work):
    if source.is_dir():
        return source
    with tarfile.open(source) as t:
        t.extractall(work, filter='data')
    (top,) = [p for p in Path(work).iterdir() if p.is_dir()]
    return top


def runtime_config(user):
    return {
        'schema': 1,
        'sunshine_binary': str(PREFIX / 'current/bin/rkmoon-kvm'),
        'worker_binary': str(PREFIX / 'current/bin/rkmoon-worker'),
        'state_directory': str(STATE),
        'capture_device': '/dev/video0',
        'capture_ownership_authorized': True,
        'allow_cpu_pixel_copy': True,
        'allow_high_resolution': True,
        'audio': {'enabled': True, 'alsa_device': 'hw:CARD=rockchiphdmiin,DEV=0'},
        'input': {'enabled': True, 'exclusive_hid_authorized': True,
                  'kvmd_socket': '/run/rkmoon-input/kvmd.sock', 'auth_headers_file': None},
        'allow_1440p90_experiment': True,
        'allow_absolute_mouse': True,
    }


def install_units(release, user):
    for name in ORDER:
        text = (release / 'systemd' / name).read_text().replace('@USER@', user)
        (UNITS / name).write_text(text)
    run('systemctl', 'daemon-reload')


def migrate_state(old, user):
    """Carry the server identity (uniqueid) so bound clients keep working."""
    info = pwd.getpwnam(user)
    STATE.mkdir(mode=0o700, exist_ok=True)
    os.chown(STATE, info.pw_uid, info.pw_gid)
    src = Path(old) / 'xdg/sunshine/sunshine_state.json'
    dst = STATE / 'xdg/sunshine/sunshine_state.json'
    if src.exists() and not dst.exists():
        for d in [STATE / 'xdg', STATE / 'xdg/sunshine']:
            d.mkdir(mode=0o700, exist_ok=True)
            os.chown(d, info.pw_uid, info.pw_gid)
        shutil.copy2(src, dst)
        os.chown(dst, info.pw_uid, info.pw_gid)
        dst.chmod(0o600)
        print('migrated server identity from', old)


def activate(version):
    target = PREFIX / 'releases' / version
    if not target.is_dir():
        raise SystemExit(f'unknown release {version}')
    tmp = PREFIX / 'current.new'
    tmp.unlink(missing_ok=True)
    tmp.symlink_to(Path('releases') / version)
    tmp.replace(PREFIX / 'current')
    print('active release', version)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--release', type=Path, help='release directory or .tar.gz from package_server.py')
    p.add_argument('--activate', metavar='VERSION', help='switch to an already installed release (rollback)')
    p.add_argument('--user', default='at', help='unprivileged streaming account')
    p.add_argument('--migrate-state', type=Path, help='old state_directory whose server identity is kept')
    p.add_argument('--start', action='store_true', help='(re)start rkmoon.service after installing')
    a = p.parse_args()
    if os.geteuid() != 0:
        raise SystemExit('run as root; the server itself runs as --user')
    if not (a.release or a.activate):
        p.error('--release or --activate required')
    pwd.getpwnam(a.user)
    for group in ['video', 'audio', 'render', 'rkmoon-input']:
        try:
            if a.user not in grp.getgrnam(group).gr_mem:
                run('usermod', '-a', '-G', group, a.user)
        except KeyError:
            print(f'warning: group {group} missing', file=sys.stderr)

    (PREFIX / 'releases').mkdir(parents=True, exist_ok=True)
    version = a.activate
    if a.release:
        with tempfile.TemporaryDirectory() as work:
            release = unpack(a.release, work)
            version = verify(release)
            dest = PREFIX / 'releases' / version
            if dest.exists():
                print('release already installed:', version)
            else:
                shutil.copytree(release, dest, symlinks=False)
                run('chown', '-R', 'root:root', str(dest))
    release = PREFIX / 'releases' / version
    verify(release)
    activate(version)

    if not CONFIG.exists():
        CONFIG.parent.mkdir(mode=0o755, exist_ok=True)
        CONFIG.write_text(json.dumps(runtime_config(a.user), indent=2) + '\n')
        CONFIG.chmod(0o644)
        print('wrote', CONFIG)
    if a.migrate_state:
        migrate_state(a.migrate_state, a.user)
    install_units(release, a.user)
    run('systemctl', 'enable', *ORDER)
    if a.start:
        run('systemctl', 'start', 'rkmoon-hdmirx-audio.service', 'rkmoon-edid.service')
        # The pool keeps buffers allocated at its first start; restarting it would free and
        # re-request them from a possibly fragmented area, so only start it when absent.
        if subprocess.run(['systemctl', 'is-active', '--quiet', 'rkmoon-capture-pool.service']).returncode:
            run('systemctl', 'start', 'rkmoon-capture-pool.service', check=False)
        run('systemctl', 'restart', 'rkmoon.service')
        run('systemctl', '--no-pager', 'status', 'rkmoon.service', check=False)
    else:
        print('installed and enabled; start with: systemctl start rkmoon.service')


if __name__ == '__main__':
    main()
