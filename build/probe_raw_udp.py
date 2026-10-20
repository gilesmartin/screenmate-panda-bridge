#!/usr/bin/env python3
"""Bounded Panda UDP receive probe: print only bus/ID/DLC counts, no CAN payload."""
import collections
import socket
import struct
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
seconds = float(sys.argv[3])
send_all = len(sys.argv) == 4 or sys.argv[4] == '--send-all'
if len(sys.argv) > 5 or (len(sys.argv) > 4 and sys.argv[4] not in ('--send-all', '--handshake-only')):
    raise SystemExit('optional argument must be --send-all or --handshake-only')
counts = collections.Counter()
active_epoch_seconds = set()
ack = False
subscription_sent = False
packets = malformed = 0
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(0.4)
s.bind(('0.0.0.0', 0))
deadline = time.monotonic() + seconds
last_hello = 0.0
while time.monotonic() < deadline:
    now = time.monotonic()
    if now - last_hello >= (3.0 if ack else 0.7):
        # The bridge expires idle peers after 10 seconds. Keep the v2 peer
        # registered throughout the bounded sample without sending CAN commands.
        s.sendto(b'ehllo', (host, port))
        last_hello = now
    try:
        packet, _ = s.recvfrom(2048)
    except socket.timeout:
        continue
    if len(packet) != 16:
        malformed += 1
        continue
    first, second = struct.unpack_from('<II', packet)
    identifier = first >> 21
    if identifier == 6:  # Panda UDP hello reply
        ack = True
        if send_all and not subscription_sent:
            # Panda v2 peer-only subscription. This is UDP control-plane state,
            # never a CAN transmit or vehicle command.
            s.sendto(bytes([0x0c]), (host, port))
            subscription_sent = True
        continue
    packets += 1
    bus = (second >> 4) & 7
    dlc = second & 15
    if dlc > 8:
        malformed += 1
        continue
    counts[(bus, identifier, dlc)] += 1
    active_epoch_seconds.add(int(time.time()))
print('handshake_ack', ack, 'frames', packets, 'malformed', malformed)
print('active_epoch_seconds', sorted(active_epoch_seconds))
print('id401_count', sum(n for (bus, identifier, dlc), n in counts.items() if identifier == 0x401))
print('bus_counts', dict(sorted(collections.Counter({bus: sum(n for (b, _, _), n in counts.items() if b == bus) for bus, _, _ in counts}).items())))
for (bus, identifier, dlc), n in sorted(counts.items()):
    print(f'bus={bus} id=0x{identifier:03x} dlc={dlc} count={n}')
