"""Registration heartbeat regression for the metadata-only Panda probe."""
import socket
import subprocess
import sys
import threading
import unittest
from pathlib import Path

PROBE = Path(__file__).resolve().parents[1] / "build" / "probe_raw_udp.py"


class ProbeHeartbeatTest(unittest.TestCase):
    def test_default_subscribes_and_receives_stock_frames(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listener.bind(("127.0.0.1", 0))
        listener.settimeout(0.3)
        stop = threading.Event()
        subscriptions = []

        def fake_server():
            while not stop.is_set():
                try:
                    data, peer = listener.recvfrom(256)
                except socket.timeout:
                    continue
                if data == b"ehllo":
                    listener.sendto((6 << 21).to_bytes(4, "little") + bytes(12), peer)
                elif data == b"\x0c":
                    subscriptions.append(peer)
                    listener.sendto((0x221 << 21).to_bytes(4, "little") +
                                    (8).to_bytes(4, "little") + bytes(8), peer)

        worker = threading.Thread(target=fake_server)
        worker.start()
        try:
            result = subprocess.run([sys.executable, str(PROBE), "127.0.0.1",
                                     str(listener.getsockname()[1]), "1"],
                                    capture_output=True, text=True, timeout=4)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(subscriptions), 1, result.stdout)
            self.assertIn("handshake_ack True frames 1 malformed 0", result.stdout)
            self.assertIn("active_epoch_seconds [", result.stdout)
        finally:
            stop.set()
            worker.join(timeout=2)
            listener.close()

    def test_renews_hello_before_server_ten_second_expiry(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listener.bind(("127.0.0.1", 0))
        listener.settimeout(0.3)
        port = listener.getsockname()[1]
        stop = threading.Event()
        hellos = []

        def fake_server():
            while not stop.is_set():
                try:
                    data, peer = listener.recvfrom(256)
                except socket.timeout:
                    continue
                if data == b"ehllo":
                    hellos.append(peer)
                    listener.sendto((6 << 21).to_bytes(4, "little") +
                                    (15 << 4).to_bytes(4, "little") + bytes(8), peer)

        worker = threading.Thread(target=fake_server)
        worker.start()
        try:
            result = subprocess.run([sys.executable, str(PROBE), "127.0.0.1", str(port), "5"],
                                    capture_output=True, text=True, timeout=9)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertGreaterEqual(len(hellos), 2, result.stdout)
        finally:
            stop.set()
            worker.join(timeout=2)
            listener.close()


if __name__ == "__main__":
    unittest.main()
