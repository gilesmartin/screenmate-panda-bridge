import mmap
import socket
import struct
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'build/raw_mcu_bridge_host'
SRC = ROOT / 'native/raw_mcu_bridge.cpp'

class RawBridgeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread',
                        '-DPANDABRIDGE_TEST_SOURCE',str(SRC),'-o',str(BIN)], check=True)

    def test_no_open_stock_preflight_is_separate_from_raw_reader(self):
        self.assertEqual(subprocess.run([str(BIN),'--check-stock-only'],
                                        capture_output=True).returncode,7)
        self.assertEqual(subprocess.run([str(BIN),'--check-stock-only',
                                         '--enable-mcu-raw'],
                                        capture_output=True).returncode,2)
        self.assertEqual(subprocess.run([str(BIN),'--check-stock-only',
                                         '--stock-session-id','1'],
                                        capture_output=True).returncode,2)

    def test_stock_present_mode_requires_explicit_opt_in_and_never_combines_modes(self):
        def run(*args):
            return subprocess.run([str(BIN), *args], capture_output=True).returncode
        self.assertEqual(run('--enable-mcu-raw','--stock-present','--source-file',
                             '/definitely/missing'), 7)
        self.assertEqual(run('--stock-present','--source-file','/definitely/missing'), 2)
        self.assertEqual(run('--enable-mcu-raw','--stock-present','--allow-absent-stock',
                             '--source-file','/definitely/missing'), 2)
        self.assertEqual(run('--enable-mcu-raw','--stock-present','--stock-session-id',
                             '1','--source-file','/definitely/missing'), 2)
        self.assertEqual(run('--enable-mcu-raw','--stock-present','--port','1338',
                             '--source-file','/definitely/missing'), 2)

    def test_production_cutover_requires_distinct_opt_in_and_stock_present_mode(self):
        def run(*args):
            return subprocess.run([str(BIN), *args], capture_output=True).returncode
        base = ('--enable-mcu-raw','--stock-present','--port','1338',
                '--source-file','/definitely/missing')
        self.assertEqual(run(*base,'--production-cutover'), 7)
        self.assertEqual(run('--stock-present','--port','1338',
                             '--production-cutover','--source-file','/definitely/missing'), 2)
        self.assertEqual(run('--enable-mcu-raw','--allow-absent-stock','--port','1338',
                             '--production-cutover','--source-file','/definitely/missing'), 2)
        self.assertEqual(run('--enable-mcu-raw','--stock-present','--port','1339',
                             '--production-cutover','--source-file','/definitely/missing'), 2)

    def test_filter_array_element_presence_precedes_size(self):
        # Installed MCU onTransact (3c7e0) reads a parcelable array; its element
        # reader (3e12c) first reads and requires a nonzero presence int32.
        # Keep this structural regression beside the target-runtime Binder tests.
        source = SRC.read_text()
        writer = source.split('// Parcelable[] vector length 1;', 1)[1].split(
            'const int32_t start=AParcel_getDataPosition(request);', 1)[0]
        self.assertEqual(writer.count('AParcel_writeInt32(request, 1)'), 2,
                         'array count and per-element presence must both be written')

    def test_service_check_not_on_ring_drain_thread(self):
        source = SRC.read_text()
        loop = source.split('while (running)', 1)[1].split(
            'raw MCU reader stopped:', 1)[0]
        self.assertNotIn('popen("/system/bin/dumpsys', source,
                         'vendor-service dumps can SIGPIPE the MCU even at preflight')
        self.assertNotIn('same_mcu_binary(', loop)
        self.assertIn('std::thread stock_monitor', source)

    def test_default_off_and_unavailable_interface_fail_closed(self):
        args=[str(BIN),'--source-file','/nonexistent','--stock-session-id','1',
              '--interfaces','lo','--port','1339']
        self.assertEqual(subprocess.run(args, capture_output=True).returncode,2)
        args.insert(1,'--enable-mcu-raw')
        args[args.index('lo')]='nonexistent_xyz'
        self.assertEqual(subprocess.run(args, capture_output=True).returncode,4)

    def test_explicit_absent_stock_mode_reaches_interface_gate(self):
        args=[str(BIN),'--enable-mcu-raw','--allow-absent-stock',
              '--source-file','/nonexistent','--interfaces','nonexistent_xyz','--port','1339']
        self.assertEqual(subprocess.run(args, capture_output=True).returncode,4)
        self.assertEqual(subprocess.run(args+['--stock-session-id','1'],
                                        capture_output=True).returncode,2)

    def test_absent_mode_uses_proc_fds_and_pid_without_dumpsys(self):
        src = (ROOT / 'native' / 'raw_mcu_bridge.cpp').read_text()
        monitor = src.split('std::thread stock_monitor([&] {', 1)[1].split('});', 1)[0]
        self.assertIn('no_can_session_fds(fd_dir)', src)
        self.assertIn('if (!same_mcu_binary() || stock_id>0) return 7;', src)
        self.assertIn('if (!no_can_session_fds(fd_dir) || !same_mcu_pid(mcu_pid)) return 7;', src)
        self.assertIn('same_mcu_pid(', monitor)
        self.assertNotIn('dumpsys', monitor)

    def test_raw_ring_to_panda_401_over_udp(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'ring'
            path.write_bytes(bytes(32+512*16))
            with path.open('r+b') as f, mmap.mmap(f.fileno(),0) as ring:
                struct.pack_into('<IIQ',ring,0,512,16,0)
                sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
                sock.bind(('127.0.0.1',0)); sock.settimeout(.25)
                reserve=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
                reserve.bind(('127.0.0.1',0)); port=reserve.getsockname()[1]; reserve.close()
                proc=subprocess.Popen([str(BIN),'--enable-mcu-raw','--source-file',str(path),
                    '--stock-session-id','1','--interfaces','lo','--port',str(port)],
                    stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
                try:
                    for _ in range(12):
                        sock.sendto(b'ehllo',('127.0.0.1',port))  # v2 with no ID filters
                        try:
                            ack,_=sock.recvfrom(128)
                            if ack[:4] == struct.pack('<I',6<<21):break
                        except socket.timeout:
                            if proc.poll() is not None:self.fail(proc.stderr.read().decode())
                    else:self.fail('no Panda ehllo ACK')
                    payload=bytes(range(8)); can_id=0x401
                    checksum=(8+can_id+sum(payload))&255
                    struct.pack_into('<BBBBI8s',ring,32,0,8,checksum,0,can_id,payload)
                    struct.pack_into('<Q',ring,8,1); ring.flush()
                    for _ in range(12):
                        try:
                            pkt,_=sock.recvfrom(128)
                            if struct.unpack_from('<I',pkt)[0]>>21==can_id:break
                        except socket.timeout:continue
                    else:self.fail('no 0x401 frame received despite no v2 ID filters')
                    self.assertEqual(len(pkt),16)
                    self.assertEqual(struct.unpack_from('<II',pkt),(0x401<<21,8))
                    self.assertEqual(pkt[8:],payload)
                finally:
                    proc.terminate(); proc.communicate(timeout=3); sock.close()

if __name__=='__main__': unittest.main()
