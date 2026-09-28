#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Foreground independent-instance supervisor. No sudo, service enable, gadget or network reconfiguration."""
import argparse
import errno
import fcntl
import json
import os
from pathlib import Path
import signal
import socket
import stat
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[1]
MARKER=b'RKMOON_DEDICATED_BUILD_v1'

class Refused(RuntimeError):pass

def load(path):
    c=json.loads(path.read_text())
    expected={'schema','sunshine_binary','worker_binary','state_directory','capture_device','capture_ownership_authorized','allow_cpu_pixel_copy','allow_high_resolution','input'}
    optional={'audio','allow_1440p90_experiment','allow_absolute_mouse'}
    if not expected <= set(c) or set(c)-expected-optional or c['schema']!=1:raise Refused('unknown/missing configuration fields')
    if type(c.get('allow_1440p90_experiment',False)) is not bool:raise Refused('experimental 90 Hz permission must be true/false')
    c.setdefault('allow_1440p90_experiment',False)
    if type(c.get('allow_absolute_mouse',False)) is not bool:raise Refused('absolute mouse permission must be true/false')
    c.setdefault('allow_absolute_mouse',False)
    audio=c.get('audio',{'enabled':False,'alsa_device':''})
    if not isinstance(audio,dict) or set(audio)!={'enabled','alsa_device'} or type(audio['enabled']) is not bool or not isinstance(audio['alsa_device'],str):raise Refused('invalid HDMI audio configuration')
    if audio['enabled'] and (not audio['alsa_device'].startswith(('hw:','plughw:')) or len(audio['alsa_device'])>128 or any(ch in audio['alsa_device'] for ch in '\r\n\x00')):raise Refused('explicit hw:/plughw: HDMI capture device required (never default microphone)')
    c['audio']=audio
    for k in ['capture_ownership_authorized','allow_cpu_pixel_copy','allow_high_resolution']:
        if type(c[k]) is not bool:raise Refused('configuration booleans must be true/false')
    i=c['input']
    if not isinstance(i,dict) or set(i)!={'enabled','exclusive_hid_authorized','kvmd_socket','auth_headers_file'}:raise Refused('invalid input configuration')
    if type(i['enabled']) is not bool or type(i['exclusive_hid_authorized']) is not bool:raise Refused('invalid input permissions')
    for k in ['sunshine_binary','worker_binary','state_directory','capture_device']:
        if not isinstance(c[k],str) or not os.path.isabs(c[k]) or any(ch in c[k] for ch in '\r\n\x00'):raise Refused('absolute local paths required')
    if not c['capture_device'].startswith('/dev/'):raise Refused('capture_device must be under /dev')
    return c

def private_state(c):
    p=Path(c['state_directory'])
    if p.is_symlink():raise Refused('state directory symlink refused')
    p.mkdir(mode=0o700,parents=True,exist_ok=True)
    s=p.stat()
    if s.st_uid!=os.getuid() or s.st_mode&0o077:raise Refused('state directory must be owned and private (0700)')
    return p

def executable(path,marker=None):
    p=Path(path);s=p.stat()
    if not stat.S_ISREG(s.st_mode) or not os.access(p,os.X_OK) or s.st_mode&(stat.S_ISUID|stat.S_ISGID|0o022):raise Refused('executable must be a regular, non-setuid, non-group/world-writable file')
    if marker and marker not in p.read_bytes():raise Refused('not the dedicated patched Sunshine binary; stock desktop fallback refused')
    try:
        if os.getxattr(p,'security.capability'):raise Refused('file capabilities not permitted on this dedicated build')
    except OSError as e:
        if e.errno not in (errno.ENODATA,errno.ENOTSUP):raise

def prepare(c,state):
    # Only create new defaults. Existing configuration and paired keys are never overwritten.
    envroot=state/'xdg';envroot.mkdir(mode=0o700,exist_ok=True)
    app={'env':{},'apps':[{'name':'HDMI','cmd':'','image-path':''}]}
    content={'apps.json':json.dumps(app,indent=2)+'\n',
        'sunshine.conf':f'sunshine_name = NanoPC-T6 HDMI KVM\nupnp = disabled\nmin_log_level = 2\nfile_apps = {state}/apps.json\n'}
    for name,text in content.items():
        p=state/name
        if p.is_symlink():raise Refused('configuration symlink refused')
        if not p.exists():
            fd=os.open(p,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
            with os.fdopen(fd,'w') as f:f.write(text)
    return envroot

def proc_start(pid):
    try:return Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()[19]
    except (OSError,IndexError):return None

def stop(state):
    record=json.loads((state/'control.json').read_text());pid=record['pid']
    if type(pid) is not int or pid<2:raise Refused('invalid supervisor pid')
    if not hasattr(os,'pidfd_open') or not hasattr(signal,'pidfd_send_signal'):raise Refused('pidfd support required; stop foreground process with Ctrl+C instead')
    fd=os.pidfd_open(pid)
    try:
        if os.stat(f'/proc/{pid}').st_uid!=os.getuid() or proc_start(pid)!=record['start']:raise Refused('stale supervisor identity; no signal sent')
        signal.pidfd_send_signal(fd,signal.SIGTERM)
    finally:os.close(fd)
    print('SIGTERM sent to verified RKMoon supervisor; no other services signalled')

def hid_command(c,state,release=False):
    i=c['input']
    if not i['enabled'] or not i['exclusive_hid_authorized']:raise Refused('explicit exclusive HID authorization required')
    args=[sys.executable,'-m','rkmoon_hid','--socket',str(state/'hid.sock'),'--kvmd-socket',i['kvmd_socket'],'--exclusive-hid-authorized']
    if i['auth_headers_file']:args+=['--headers',i['auth_headers_file']]
    if release:args+=['--release-all']
    return args

def base_env(c,state):
    e=os.environ.copy();e['PYTHONPATH']=str(ROOT/'python');e['HOME']=str(state);e['XDG_CONFIG_HOME']=str(state/'xdg')
    # The Linux privileged environment sanitizer remains upstream; runtime must not require root/capabilities.
    e.update(RKMOON_WORKER=c['worker_binary'],RKMOON_VIDEO_DEVICE=c['capture_device'],RKMOON_CAPTURE_AUTHORIZED='1',
        RKMOON_ALLOW_COPY=str(int(c['allow_cpu_pixel_copy'])),RKMOON_ALLOW_HIGH_RES=str(int(c['allow_high_resolution'])),
        RKMOON_ALLOW_1440P90_EXPERIMENT=str(int(c.get('allow_1440p90_experiment',False))),
        RKMOON_ALLOW_ABSOLUTE_MOUSE=str(int(c.get('allow_absolute_mouse',False) and c['input']['enabled'])))
    e.pop('CONFIGURATION_DIRECTORY',None)
    e.pop('SUNSHINE_MIGRATE_CONFIG',None)
    e.pop('RKMOON_HID_SOCKET',None)
    e.pop('RKMOON_AUDIO_DEVICE',None)
    e.pop('RKMOON_ADMIN_SOCKET',None)
    if c['audio']['enabled']:e['RKMOON_AUDIO_DEVICE']=c['audio']['alsa_device']
    if c['input']['enabled']:e['RKMOON_HID_SOCKET']=str(state/'hid.sock')
    # ADR-013 boot-time capture buffers; the worker falls back to per-session allocation without it.
    e['RKMOON_CAPTURE_POOL']='/run/rkmoon-capture-pool/pool.sock'
    return e

def check_ports():
    # Only GameStream HTTP/HTTPS + RTSP; port 47990 Web UI is not started.
    # No firewall/UPnP changes. Additional upstream listeners may still fail at startup.
    for kind,ports in [(socket.SOCK_STREAM,[47989,48010]),(socket.SOCK_DGRAM,[47998,47999,48000])]:
        for port in ports:
            with socket.socket(socket.AF_INET,kind) as s:
                if kind==socket.SOCK_STREAM:
                    s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
                try:s.bind(('0.0.0.0',port))
                except OSError:raise Refused(f'Port {port} is occupied; no owner was stopped') from None

def stale_socket(path):
    if not path.exists():return
    s=path.lstat()
    if not stat.S_ISSOCK(s.st_mode) or s.st_uid!=os.getuid():raise Refused('unknown socket path owner/type')
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as sock:
        sock.settimeout(.2)
        try:sock.connect(str(path))
        except ConnectionRefusedError:path.unlink();return
        except OSError:raise Refused('cannot verify stale socket; leave it intact') from None
    raise Refused('HID bridge already listening; leave it intact')

def start(c,state):
    if os.geteuid()==0:raise Refused('run unprivileged; grant specific device ACLs manually, not root/setcap')
    if not c['capture_ownership_authorized']:raise Refused('capture ownership has not been granted; no child/device opened')
    executable(c['sunshine_binary'],MARKER);executable(c['worker_binary'])
    if c['input']['enabled'] and not c['input']['exclusive_hid_authorized']:raise Refused('HID exclusivity not granted')
    envroot=prepare(c,state);check_ports();env=base_env(c,state)
    children=[];logs=[];stopping=False;failed=False;hid_started=False
    # Release layout ships Sunshine's runtime libraries and MPP in ROOT/lib. Only the native
    # server (and the worker it starts) sees them; the Python HID bridge keeps system libraries.
    native_env=dict(env)
    if (ROOT/'lib').is_dir():native_env['LD_LIBRARY_PATH']=str(ROOT/'lib')
    def spawn(args,logname,child_env=env):
        log=(state/logname).open('ab',buffering=0);logs.append(log)
        p=subprocess.Popen([sys.executable,str(ROOT/'tools/child_exec.py'),str(os.getpid()),'--',*args],env=child_env,cwd=state,stdout=log,stderr=subprocess.STDOUT)
        children.append(p);return p
    def on_signal(sig,frame):
        nonlocal stopping
        stopping=True
    for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,on_signal)
    record={'pid':os.getpid(),'start':proc_start(os.getpid())}
    (state/'control.json').write_text(json.dumps(record));(state/'control.json').chmod(0o600)
    try:
        if c['input']['enabled']:
            stale_socket(state/'hid.sock');hid=spawn(hid_command(c,state),'hid.log');hid_started=True
            (state/'hid-recovery-needed.json').write_text(json.dumps({'supervisor_pid':os.getpid(),'reason':'owned input instance requires cleanup'}))
            deadline=time.monotonic()+65
            while not (state/'hid.sock').exists():
                if hid.poll() is not None or stopping or time.monotonic()>deadline:raise Refused('HID bridge did not become ready; inspect sanitized local log')
                time.sleep(.05)
        sunshine=spawn([c['sunshine_binary'],str(state/'sunshine.conf')],'sunshine.log',native_env)
        print('RKMoon minimal host started; no Web UI. Connect with the password-enabled client. Ctrl+C stops this instance.',flush=True)
        while not stopping:
            if any(p.poll() is not None for p in children):failed=True;break
            time.sleep(.1)
    finally:
        # Terminate Sunshine first so its authenticated input session closes before stopping the HID bridge.
        for child in reversed(children):
            if child.poll() is None:child.terminate()
            try:child.wait(4)
            except subprocess.TimeoutExpired:child.kill();child.wait()
        if hid_started:
            # Separate recovery attempt covers a killed HID child. Failure is reported, not treated as release success.
            try:
                r=subprocess.run(hid_command(c,state,True),env=env,timeout=70,stdout=subprocess.DEVNULL)
                if r.returncode:failed=True;print('WARNING: USB release not confirmed; keep input ownership blocked',file=sys.stderr)
                else:(state/'hid-recovery-needed.json').unlink(missing_ok=True)
            except subprocess.TimeoutExpired:failed=True;print('WARNING: release timeout; keep input ownership blocked',file=sys.stderr)
        for log in logs:log.close()
        (state/'control.json').unlink(missing_ok=True)
    return 1 if failed else 0

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['prepare','start','stop','release-all']);p.add_argument('--config',type=Path,required=True);a=p.parse_args()
    os.umask(0o077);c=load(a.config);state=private_state(c)
    if a.action=='stop':stop(state);return 0
    # The lock is local-instance ownership, NOT an assertion that old capture/USB services are idle.
    lockfd=os.open(state/'supervisor.lock',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
    try:
        try:fcntl.flock(lockfd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise Refused('this independent instance is already running') from None
        if a.action=='prepare':prepare(c,state);print('Private defaults prepared; no service started/enabled');return 0
        if a.action=='release-all':
            code=subprocess.run(hid_command(c,state,True),env=base_env(c,state)).returncode
            if code==0:(state/'hid-recovery-needed.json').unlink(missing_ok=True)
            return code
        return start(c,state)
    finally:os.close(lockfd)
if __name__=='__main__':
    try:raise SystemExit(main())
    except (Refused,OSError,ValueError,KeyError) as e:raise SystemExit(f'RKMoon refused: {e}')
