#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Hold HDMI capture buffers allocated from the CMA heap at boot (ADR-013).

The HDMI receiver DMAs into physically contiguous memory. On a host that has run for
days, the CMA area is fragmented by pages the kernel cannot migrate, and a fresh 6 MB
(1080p BGR) or 25 MB (4K BGR) contiguous allocation fails with EBUSY. This service
allocates the buffers once, early at boot, keeps them for the life of the system and
lends them to the streaming account's worker over a private Unix socket (SCM_RIGHTS).

Runs as root only to open /dev/dma_heap/reserved. It never changes the CMA size, boot
arguments or any kernel setting. Only the configured user may connect.
"""
import argparse
import fcntl
import json
import os
import pwd
import socket
import struct
import sys
import time

HEAP = '/dev/dma_heap/reserved'
# DMA_HEAP_IOCTL_ALLOC = _IOWR('H', 0, struct dma_heap_allocation_data{u64 len; u32 fd; u32 fd_flags; u64 heap_flags;})
ALLOC = (3 << 30) | (24 << 16) | (ord('H') << 8) | 0
# Largest source mode the EDID offers is 3840x2160; BGR24 is the largest capture format.
DEFAULT_SIZE = (3840 * 2160 * 3 + 4095) // 4096 * 4096
DEFAULT_COUNT = 4


def allocate(count, size):
    heap = os.open(HEAP, os.O_RDWR | os.O_CLOEXEC)
    fds = []
    try:
        for _ in range(count):
            data = bytearray(struct.pack('<QIIQ', size, 0, os.O_RDWR | os.O_CLOEXEC, 0))
            fcntl.ioctl(heap, ALLOC, data)
            fds.append(struct.unpack('<QIIQ', data)[1])
        return fds
    except OSError:
        for fd in fds:
            os.close(fd)
        raise
    finally:
        os.close(heap)


def allocate_with_retry(count, size, attempts):
    for attempt in range(1, attempts + 1):
        try:
            return allocate(count, size)
        except OSError as error:
            print(f'capture pool: allocation attempt {attempt}/{attempts} failed: {error.strerror}', flush=True)
            if attempt == attempts:
                raise
            # Dropping clean page cache lets the kernel migrate more CMA pages; it does not
            # change any setting and is what an operator would do by hand.
            os.sync()
            with open('/proc/sys/vm/drop_caches', 'w') as f:
                f.write('3')
            time.sleep(1)


def serve(path, user, fds, size):
    info = pwd.getpwnam(user)
    directory = os.path.dirname(path)
    os.makedirs(directory, mode=0o700, exist_ok=True)
    os.chown(directory, info.pw_uid, info.pw_gid)
    os.chmod(directory, 0o700)
    if os.path.lexists(path):
        os.unlink(path)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM | socket.SOCK_CLOEXEC)
    server.bind(path)
    os.chown(path, info.pw_uid, info.pw_gid)
    os.chmod(path, 0o600)
    server.listen(4)
    header = json.dumps({'version': 1, 'count': len(fds), 'size': size}).encode()
    print(f'capture pool ready: {len(fds)} x {size} bytes for {user}', flush=True)
    while True:
        conn, _ = server.accept()
        with conn:
            pid, uid, gid = struct.unpack('3i', conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if uid != info.pw_uid:
                continue
            try:
                socket.send_fds(conn, [header], fds)
            except OSError:
                pass


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--user', required=True)
    p.add_argument('--socket', default='/run/rkmoon-capture-pool/pool.sock')
    p.add_argument('--count', type=int, default=DEFAULT_COUNT)
    p.add_argument('--size', type=int, default=DEFAULT_SIZE)
    p.add_argument('--attempts', type=int, default=5)
    a = p.parse_args()
    if os.geteuid() != 0:
        sys.exit('capture pool must run as root to open ' + HEAP)
    if not 2 <= a.count <= 4 or not 1 << 20 <= a.size <= 64 << 20 or a.size % 4096:
        sys.exit('count must be 2..4 and size a 4 KiB multiple between 1 and 64 MiB')
    fds = allocate_with_retry(a.count, a.size, a.attempts)
    serve(a.socket, a.user, fds, a.size)


if __name__ == '__main__':
    main()
