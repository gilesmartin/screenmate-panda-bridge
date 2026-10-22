import importlib.util,json,subprocess,sys,tempfile,unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1]
HASH='18846138ffe5333e3be636e3bfb91d9b2e4b046fb2107988ed13cb71e8d99617'
class CommunityTest(unittest.TestCase):
 def test_checker_matching_fixture_is_not_deployment_permission(self):
  self.assertTrue((R/'scripts/compatibility.py').is_file(),'missing read-only community checker')
  with tempfile.TemporaryDirectory() as td:
   f=Path(td)/'fixture.json';f.write_text(json.dumps(dict(uid='0',abi='arm64-v8a',api='34',selinux='Enforcing',mcu_sha256=HASH,ap0='yes',tun1='no')))
   p=subprocess.run([sys.executable,str(R/'scripts/compatibility.py'),'--fixture',str(f)],capture_output=True,text=True)
   self.assertEqual(p.returncode,0,p.stderr);j=json.loads(p.stdout);self.assertEqual(j['status'],'fingerprint-match');self.assertFalse(j['ready_for_install']);self.assertEqual(j['evidence_origin'],'fixture');self.assertFalse(j['may_open_session'])

class BuildToolTest(unittest.TestCase):
 def test_missing_ndk_fails_without_output(self):
  self.assertTrue((R/'scripts/build-android.py').is_file(),'missing reproducible Android cross-build')
  with tempfile.TemporaryDirectory() as td:
   out=Path(td)/'out'
   p=subprocess.run([sys.executable,str(R/'scripts/build-android.py'),'--ndk',str(Path(td)/'absent'),'--output',str(out)],capture_output=True,text=True)
   self.assertNotEqual(p.returncode,0);self.assertFalse(out.exists());self.assertIn('NDK',p.stderr)

class CheckerBoundaryTest(unittest.TestCase):
 def invoke(self,value):
  with tempfile.TemporaryDirectory()as td:
   f=Path(td)/'fixture.json';f.write_text(json.dumps(value));p=subprocess.run([sys.executable,str(R/'scripts/compatibility.py'),'--fixture',str(f)],capture_output=True,text=True);return p,json.loads(p.stdout)
 def test_unknown_or_missing_firmware_never_matches(self):
  for obj in [{},{'mcu_sha256':'0'*64},[],{'mcu_sha256':HASH}]:
   p,j=self.invoke(obj);self.assertNotEqual(p.returncode,0);self.assertFalse(j['ready_for_install']);self.assertFalse(j['may_open_session'])
 def test_each_baseline_field_is_required(self):
  base=dict(uid='0',abi='arm64-v8a',api='34',selinux='Enforcing',mcu_sha256=HASH,ap0='yes',tun1='no')
  for k in ['uid','abi','api','selinux','mcu_sha256','ap0']:
   value=dict(base);value.pop(k);p,j=self.invoke(value);self.assertNotEqual(p.returncode,0);self.assertFalse(j['baseline_prerequisites_match'])
 def test_readonly_adb_allowlist_and_safe_transport_failure(self):
  with tempfile.TemporaryDirectory()as td:
   f=Path(td)/'fakeadb';calls=Path(td)/'calls';f.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "'+str(calls)+'"\nexit 1\n');f.chmod(0o700)
   p=subprocess.run([sys.executable,str(R/'scripts/compatibility.py'),'--adb',str(f),'--serial','private-test-device'],capture_output=True,text=True);j=json.loads(p.stdout);self.assertEqual(p.returncode,4);self.assertFalse(j['ready_for_install']);self.assertNotIn('private-test-device',p.stdout+p.stderr)
   args=calls.read_text();self.assertEqual(args.splitlines()[:4],['-s','private-test-device','shell',"printf 'uid='; id -u"])
   for denied in ['dumpsys','ctl.restart','setprop','enable-mcu-raw','kill ','su -c','cat /data/']:
    self.assertNotIn(denied,args)
 def test_malformed_fixture_is_closed(self):
  with tempfile.TemporaryDirectory()as td:
   f=Path(td)/'fixture.json';f.write_text('{');p=subprocess.run([sys.executable,str(R/'scripts/compatibility.py'),'--fixture',str(f)],capture_output=True,text=True);self.assertEqual(p.returncode,4);self.assertFalse(json.loads(p.stdout)['may_open_session'])
class PackagingBoundaryTest(unittest.TestCase):
 def test_build_does_not_execute_or_deploy_binaries(self):
  t=(R/'scripts/build-android.py').read_text()
  for denied in ['adb','scp','AIBinder','PANDABRIDGE_TEST_SOURCE','--enable-mcu-raw']:
   self.assertNotIn(denied,t)
  for expected in ['NOTICE.toolchain','-static-libstdc++','28.2.13676358','-ffile-prefix-map','sha256']:
   self.assertIn(expected,t)
 def test_document_links_exist(self):
  import re
  for f in [R/'README.md']+list((R/'docs').glob('*.md')):
   for dest in re.findall(r'\]\(([^)]+)\)',f.read_text()):
    if ':'in dest or dest.startswith('#'):continue
    self.assertTrue((f.parent/dest.split('#')[0]).exists(),str(f.relative_to(R))+': '+dest)
 def test_no_vendor_dumps_in_operational_scripts(self):
  for f in(R/'scripts').glob('*'):
   if f.suffix not in ['.py','.sh']:continue
   text=f.read_text()
   self.assertNotIn('ctl.restart',text);self.assertNotIn('setprop ',text)
if __name__=='__main__':unittest.main()
