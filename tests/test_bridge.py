import mmap
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "native" / "panda_bridge.cpp"
BIN = ROOT / "build" / "panda_bridge_test"


def panda_decode(packet):
    assert len(packet) == 16, f"expected one 16-byte Panda frame, got {len(packet)}"
    address, bus_dlc = struct.unpack_from("<II", packet)
    return address >> 21, bus_dlc >> 4, bus_dlc & 15, packet[8:16]


class BridgeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        assert SRC.exists(), "missing native Panda bridge implementation"
        BIN.parent.mkdir(exist_ok=True)
        subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                        "-DPANDABRIDGE_TEST_SOURCE=1", str(SRC), "-o", str(BIN)], check=True)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fixture = Path(self.tmp.name) / "ring"
        with self.fixture.open("wb") as f:
            f.truncate(4096)
        self.ring_fd = self.fixture.open("r+b")
        self.ring = mmap.mmap(self.ring_fd.fileno(), 4096)
        struct.pack_into("<IIQ", self.ring, 0, 64, 16, 0)
        reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        reserve.bind(("127.0.0.1", 0))
        self.port = reserve.getsockname()[1]
        reserve.close()
        self.proc = subprocess.Popen([str(BIN), "--interfaces", "lo", "--port", str(self.port),
                                      "--source-file", str(self.fixture)],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.25)

    def tearDown(self):
        self.sock.close()
        self.proc.terminate()
        try:
            self.proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.communicate(timeout=3)
        self.ring.close()
        self.ring_fd.close()
        self.tmp.cleanup()

    def hello(self, message=b"hello"):
        for _ in range(16):
            self.sock.sendto(message, ("127.0.0.1", self.port))
            try:
                packet, peer = self.sock.recvfrom(2048)
                self.assertEqual(peer[0], "127.0.0.1")
                return panda_decode(packet)
            except socket.timeout:
                if self.proc.poll() is not None:
                    self.fail("bridge exited before handshake: " + self.proc.stderr.read().decode()[-1000:])
        self.fail("bridge did not acknowledge hello")

    def write_frame(self, sequence, bus=0, can_id=0x213, data=b"\xAA\xBB"):
        raw = (bus << 12) | can_id
        payload = data.ljust(8, b"\0")
        checksum = (len(data) + raw + sum(payload)) & 255
        offset = 32 + ((sequence - 1) & 63) * 16
        struct.pack_into("<BBBBI8s", self.ring, offset, bus, len(data), checksum, 0, raw, payload)
        struct.pack_into("<Q", self.ring, 8, sequence)
        self.ring.flush()

    def receive_frame(self, expected_id):
        end = time.monotonic() + 2
        while time.monotonic() < end:
            try:
                packet, _ = self.sock.recvfrom(2048)
                decoded = panda_decode(packet)
                if decoded[0] == expected_id:
                    return decoded
            except socket.timeout:
                pass
        self.fail(f"no Panda frame with CAN ID {expected_id:#x}")

    def test_v1_handshake_and_read_only_frame(self):
        self.assertEqual(self.hello(), (6, 15, 0, b"\0" * 8))
        self.write_frame(1)
        self.assertEqual(self.receive_frame(0x213), (0x213, 0, 2, b"\xAA\xBB" + b"\0" * 6))

    def test_v2_filters_clear_and_send_all_preserve_bus(self):
        self.assertEqual(self.hello(b"ehllo"), (6, 15, 0, b"\0" * 8))
        self.write_frame(1, can_id=0x213)
        with self.assertRaises(socket.timeout):
            self.sock.recvfrom(2048)  # v2 starts filtered, like upstream CANserver
        self.sock.sendto(bytes([0x0f, 0, 0x02, 0x13]), ("127.0.0.1", self.port))
        time.sleep(0.05)
        self.write_frame(2, can_id=0x213, data=b"\x10\x20")
        self.assertEqual(self.receive_frame(0x213), (0x213, 0, 2, b"\x10\x20" + b"\0" * 6))
        self.sock.sendto(b"\x18", ("127.0.0.1", self.port))
        self.sock.sendto(b"\x0c", ("127.0.0.1", self.port))
        time.sleep(0.05)
        self.write_frame(3, bus=3, can_id=0x139, data=b"\x42")
        self.assertEqual(self.receive_frame(0x139), (0x139, 3, 1, b"\x42" + b"\0" * 7))

    def test_unregistered_client_does_not_receive_raw_frames(self):
        self.write_frame(1)
        with self.assertRaises(socket.timeout):
            self.sock.recvfrom(2048)


if __name__ == "__main__":
    unittest.main()
