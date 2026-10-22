#!/usr/bin/env python3
"""Read-only fingerprint report, NEVER an installation/session-open authorization."""
import argparse,json,re,subprocess
from pathlib import Path
PIN='18846138ffe5333e3be636e3bfb91d9b2e4b046fb2107988ed13cb71e8d99617'
# Fixed allowlist: no user text is interpolated into the target shell command.
COLLECTOR='''printf 'uid='; id -u
printf 'abi='; getprop ro.product.cpu.abi
printf 'api='; getprop ro.build.version.sdk
printf 'selinux='; getenforce
printf 'mcu_sha256='; sha256sum /odm/bin/hw/vendor.teslogic.hardware.mcu.service 2>/dev/null | cut -d ' ' -f 1
for n in ap0 tun1; do printf '%s=' "$n"; if [ -d "/sys/class/net/$n" ]; then printf 'yes\n'; else printf 'no\n'; fi; done
'''
def classify(data,origin):
 expected={'uid':'0','abi':'arm64-v8a','api':'34','selinux':'Enforcing','mcu_sha256':PIN}
 reasons=[key+' differs from tested baseline or is unavailable'for key,value in expected.items()if data.get(key)!=value]
 if not any(data.get(key)=='yes'for key in ['ap0','tun1']):reasons.append('no approved listener interface observed')
 return {'schema':1,'evidence_origin':origin,'status':'fingerprint-match'if not reasons else'not-established','firmware_fingerprint_matches':data.get('mcu_sha256')==PIN,'baseline_prerequisites_match':not reasons,'ready_for_install':False,'may_open_session':False,'read_only':True,'limitations':['This does not inspect session ownership, prove a safe handover, or test client delivery.','Do not invoke vendor MCU dumpsys or replace the firmware pin.','A fixture report is offline test evidence, not a device check.'],'reasons':reasons}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--adb',default='adb');g=p.add_mutually_exclusive_group(required=True);g.add_argument('--serial');g.add_argument('--fixture',type=Path);a=p.parse_args()
 try:
  if a.fixture:
   data=json.loads(a.fixture.read_text());origin='fixture'
   if not isinstance(data,dict):raise ValueError('invalid fixture')
  else:
   q=subprocess.run([a.adb,'-s',a.serial,'shell',COLLECTOR],stdin=subprocess.DEVNULL,capture_output=True,text=True,timeout=15)
   if q.returncode or len(q.stdout)>8192:raise ValueError('transport failed')
   data={}
   for line in q.stdout.splitlines():
    if '='in line:
     k,v=line.split('=',1)
     if k in data:raise ValueError('duplicate output field')
     if k in {'uid','abi','api','selinux','mcu_sha256','ap0','tun1'}:data[k]=v.strip()
   origin='device-read-only'
  result=classify(data,origin);print(json.dumps(result,indent=2));return 0 if result['baseline_prerequisites_match']else 3
 except (OSError,ValueError,subprocess.TimeoutExpired):
  print(json.dumps({'schema':1,'status':'unavailable','ready_for_install':False,'may_open_session':False,'read_only':True,'reason':'Transport or report unavailable; no deployment was attempted.'}));return 4
if __name__=='__main__':raise SystemExit(main())
