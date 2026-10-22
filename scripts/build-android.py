#!/usr/bin/env python3
"""Cross-build own-source ARM64 Android artifacts; never executes/deploys them."""
import argparse,hashlib,json,os,platform,shutil,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REVISION='28.2.13676358'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--ndk',type=Path,default=os.environ.get('NDK_ROOT'));p.add_argument('--output',type=Path,default=ROOT/'dist/android-arm64');p.add_argument('--api',type=int,choices=[34],default=34);a=p.parse_args()
 if not a.ndk:p.error('NDK required: --ndk or NDK_ROOT')
 ndk=a.ndk.expanduser().resolve();host={'Darwin':'darwin-x86_64','Linux':'linux-x86_64'}.get(platform.system());out=a.output.expanduser().absolute()
 props=ndk/'source.properties'
 if not props.is_file()or('Pkg.Revision = '+REVISION)not in props.read_text():p.error('NDK '+REVISION+' (r28c) is required')
 if not host:p.error('NDK build host must be macOS or Linux')
 compiler=ndk/'toolchains/llvm/prebuilt'/host/'bin/aarch64-linux-android34-clang++'
 notice_paths=[ndk/'NOTICE',ndk/'NOTICE.toolchain',ndk/'toolchains/llvm/prebuilt'/host/'NOTICE']
 if not compiler.is_file()or not all(x.is_file()for x in notice_paths):p.error('NDK compiler or license notices missing')
 if out.exists()or out.is_symlink():p.error('output already exists; choose a fresh build directory')
 version=subprocess.check_output([str(compiler),'--version'],text=True,timeout=15).splitlines()[0]
 out.parent.mkdir(parents=True,exist_ok=True)
 try:
  with tempfile.TemporaryDirectory(prefix='.panda-build-',dir=out.parent)as td:
   stage=Path(td);files={}
   flags=['-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread','-static-libstdc++','-Wl,--build-id=sha1','-ffile-prefix-map='+str(ROOT)+'=.','-fdebug-prefix-map='+str(ROOT)+'=.']
   for name in ['panda_bridge','raw_mcu_bridge']:
    dest=stage/name;subprocess.run([str(compiler)]+flags+[str(ROOT/'native'/(name+'.cpp')),'-o',str(dest)]+(['-lbinder_ndk','-llog','-ldl','-landroid']if name=='raw_mcu_bridge'else[]),check=True,timeout=180)
    header=dest.read_bytes();assert header[:6]==b'\x7fELF\x02\x01'and struct.unpack_from('<H',header,18)[0]==183,'not little-endian ARM64 ELF'
    assert str(Path.home()).encode()not in header,'private host path in ELF'
    dest.chmod(0o755);files[name]={'sha256':digest(dest),'bytes':dest.stat().st_size,'elf':'ELF64 little-endian AArch64'}
   notices=stage/'NDK-NOTICES';notices.mkdir()
   for i,source in enumerate(notice_paths):shutil.copyfile(source,notices/(str(i+1)+'-'+source.name))
   shutil.copyfile(ROOT/'LICENSE',stage/'LICENSE')
   metadata={'schema':1,'kind':'experimental developer cross-build, NOT an installer','ndk_revision':REVISION,'android_api':34,'abi':'arm64-v8a','compiler':version,'static_cpp_runtime':True,'test_source_macro':False,'can_tx':False,'deployed_by_build':False,'files':files,'native_source_sha256':{str(f.relative_to(ROOT)):digest(f)for f in sorted((ROOT/'native').iterdir())if f.is_file()},'notice_sha256':{str(f.relative_to(stage)):digest(f)for f in sorted(notices.iterdir())}}
   (stage/'BUILD-METADATA.json').write_text(json.dumps(metadata,indent=2)+'\n');(stage/'README.txt').write_text('Experimental ScreenMate RX-only Panda UDP developer build. Not an APK, Magisk module or installer. Read the matching source release documentation. Do not execute without owner-specific firmware, root, stock ownership, supervision and rollback review. Default raw open is disabled. No MCU dump, reset, vendor firmware or vehicle credentials are included. These newly cross-built binaries have NOT been installed or vehicle-tested by this build.\n')
   checks=[f for f in sorted(stage.rglob('*'))if f.is_file()];(stage/'SHA256SUMS').write_text(''.join(digest(f)+'  '+str(f.relative_to(stage))+'\n'for f in checks));os.rename(stage,out)
  print(json.dumps({'built':list(files),'output':str(out),'android_api':34,'abi':'arm64-v8a','deployed':False}));return 0
 except (OSError,ValueError,AssertionError,subprocess.SubprocessError)as error:
  p.exit(1,'NDK build failed; no device action was performed: '+type(error).__name__+'\n')
if __name__=='__main__':raise SystemExit(main())
