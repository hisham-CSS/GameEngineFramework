"""Two processes, one match, over the built-in UDP adapter on 127.0.0.1 (ROADMAP M2.1).

    python tests/two_peers.py <path to online_peer> [ticks]

Launches one `online_peer` per slot on two loopback ports, waits for both, and
requires: both exit 0 (no desync, no timeout), both print a checksum for the
same frame, and the two checksums are equal. That is the transport carrying a
real rollback match between two kernels in two processes -- the property the
spike exists to measure. Standard library only; ctest runs it in the Windows
job (the Linux job compiles and does not run tests).
"""
import re
import subprocess
import sys

PORTS = (47011, 47012)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    exe = sys.argv[1]
    ticks = sys.argv[2] if len(sys.argv) > 2 else '300'
    procs = []
    for slot in (0, 1):
        procs.append(subprocess.Popen(
            [exe, '--slot', str(slot), '--port', str(PORTS[slot]),
             '--peer', '127.0.0.1:%d' % PORTS[1 - slot], '--ticks', ticks],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
    results = []
    for slot, p in enumerate(procs):
        try:
            out, err = p.communicate(timeout=120)
        except subprocess.TimeoutExpired:
            p.kill()
            out, err = p.communicate()
            raise SystemExit('two_peers: peer %d did not finish in time\n%s' % (slot, err))
        m = re.search(r'checksum ([0-9a-f]+) frame (\d+) connected (\d+)', out)
        print('peer %d: exit %d, %s' % (slot, p.returncode, out.strip() or err.strip()))
        if p.returncode != 0 or not m:
            raise SystemExit('two_peers: peer %d failed (exit %d): %s' % (slot, p.returncode, err.strip()))
        results.append((m.group(1), int(m.group(2)), int(m.group(3))))
    (sum0, frame0, conn0), (sum1, frame1, conn1) = results
    if frame0 != frame1:
        raise SystemExit('two_peers: the peers reported different frames (%d vs %d)' % (frame0, frame1))
    if sum0 != sum1:
        raise SystemExit('two_peers: the peers disagree at frame %d: %s vs %s' % (frame0, sum0, sum1))
    if conn0 < 1 or conn1 < 1:
        raise SystemExit('two_peers: a peer finished without ever counting the other as connected')
    print('two_peers: both peers at frame %d with checksum %s over UDP -- OK' % (frame0, sum0))


if __name__ == '__main__':
    main()
