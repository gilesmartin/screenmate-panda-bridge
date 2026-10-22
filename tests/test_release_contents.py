"""Fail-closed checks for the exact source-only public tree."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ALLOWED = {
    'scripts/compatibility.py',
    'scripts/build-android.py',
    'scripts/test-host.sh',
    'tests/test_community_packaging.py',
    '.github/workflows/ci.yml',
    'THIRD_PARTY_NOTICES.md',
    'CHANGELOG.md',
    'docs/architecture.md',
    'docs/build.md',
    'docs/compatibility.md',
    'docs/client-setup.md',
    'docs/boot-and-recovery.md',
    'docs/validation.md',
    'docs/security.md',
    'README.md', 'LICENSE', '.gitignore',
    'native/panda_bridge.cpp', 'native/raw_mcu_bridge.cpp',
    'native/raw_ring.h', 'native/stock_guard.h',
    'native/stock_proc_snapshot.h', 'native/session_inventory.h',
    'native/proc_fd_inventory.h',
    'build/probe_raw_udp.py',
    'tests/test_bridge.py', 'tests/test_raw_bridge.py',
    'tests/test_probe_raw_udp.py', 'tests/test_release_contents.py',
    'tests/test_raw_mcu_ring.cpp', 'tests/test_stock_guard.cpp',
    'tests/test_session_inventory.cpp', 'tests/test_proc_fd_inventory.cpp',
}


class ReleaseContentsTest(unittest.TestCase):
    def test_exact_public_allowlist(self):
        files = {str(p.relative_to(ROOT)) for p in ROOT.rglob('*')
                 if p.is_file() and '.git' not in p.parts and '__pycache__' not in p.parts
                 and p.suffix not in ('.pyc',)
                 and 'dist' not in p.relative_to(ROOT).parts
                 and (p.parent != ROOT / 'build' or p.name == 'probe_raw_udp.py')}
        self.assertEqual(files, ALLOWED)

    def test_no_private_identifiers_or_binary_payloads(self):
        for path in ROOT.rglob('*'):
            if not path.is_file() or '.git' in path.parts or '__pycache__' in path.parts:
                continue
            if str(path.relative_to(ROOT)) not in ALLOWED:
                continue
            data = path.read_bytes()
            self.assertNotIn(b'\x00', data, str(path))
            for marker in (b'martins' + b'.ink', b'robert' + b'martin', b'/Users/' + b'robert',
                           b'/home/' + b'hermes', b'BEGIN ' + b'PRIVATE KEY'):
                self.assertNotIn(marker, data, str(path))

    def test_readme_is_non_turnkey_and_discloses_limits(self):
        text = (ROOT / 'README.md').read_text().lower()
        for phrase in ('experimental', 'no installer', 'root', 'firmware',
                       'fallback', 'not fault-tested', 'receive-only'):
            self.assertIn(phrase, text)


if __name__ == '__main__':
    unittest.main()
