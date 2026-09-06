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


def launch(exe, ticks, extra=()):
    procs = []
    for slot in (0, 1):
        args = [exe, '--slot', str(slot), '--port', str(PORTS[slot]),
                '--peer', '127.0.0.1:%d' % PORTS[1 - slot], '--ticks', ticks]
        procs.append(subprocess.Popen(args + list(extra[slot] if extra else []),
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
    return procs


def mismatch_scenario(exe, ticks):
    """A5 over a real wire (ROADMAP M2.2): peer 1 offers a different content
    hash; BOTH peers must refuse, naming the hash, before any session exists."""
    procs = launch(exe, ticks, extra=([], ['--content-mismatch']))
    for slot, p in enumerate(procs):
        try:
            out, err = p.communicate(timeout=60)
        except subprocess.TimeoutExpired:
            p.kill()
            raise SystemExit('two_peers: mismatch scenario: peer %d did not finish' % slot)
        print('peer %d (mismatch): exit %d, %s' % (slot, p.returncode, err.strip() or out.strip()))
        if p.returncode != 4 or 'content hash' not in err:
            raise SystemExit('two_peers: peer %d did not refuse the mismatch by name (exit %d): %s' % (slot, p.returncode, err.strip()))
    print('two_peers: the content mismatch was refused by both peers, naming the hash -- OK')


def diverge_scenario(exe, ticks):
    """T6 over a real wire (ROADMAP M2.3): peer 1's kernel drifts from frame 100;
    BOTH peers must stop, swap states and name p[1].health in their artifact."""
    procs = launch(exe, ticks, extra=([], ['--diverge']))
    for slot, p in enumerate(procs):
        try:
            out, err = p.communicate(timeout=90)
        except subprocess.TimeoutExpired:
            p.kill()
            raise SystemExit('two_peers: diverge scenario: peer %d did not finish' % slot)
        print('peer %d (diverge): exit %d, %s' % (slot, p.returncode, err.strip() or out.strip()))
        if p.returncode != 3 or 'field p[1].health' not in err:
            raise SystemExit('two_peers: peer %d did not name the divergent field (exit %d): %s' % (slot, p.returncode, err.strip()))
    print('two_peers: the drift was reported by both peers, each naming p[1].health -- OK')


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    exe = sys.argv[1]
    ticks = sys.argv[2] if len(sys.argv) > 2 else '300'
    mismatch_scenario(exe, ticks)
    diverge_scenario(exe, ticks)
    procs = launch(exe, ticks)
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
