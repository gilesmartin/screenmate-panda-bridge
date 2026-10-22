# Build the Android ARM64 preview

## Scope

The build helper cross-compiles **only project-owned** `raw_mcu_bridge` and `panda_bridge` for Android ARM64/API 34. It does not install, execute on, or contact a device. The intended release is a source ZIP plus an Android ARM64 archive containing the two binaries, `BUILD-METADATA.json` (including hashes), and NDK license notices. It is **not** an APK, Magisk module, installer, or boot service.

## Requirements

- macOS or Linux build host and Python 3.9 or later (full socket/ring host tests require Linux)
- Android NDK **28.2.13676358 (r28c)**, accessible through `NDK_ROOT`
- The NDK's Android ARM64 toolchain and API 34 sysroot

Do not substitute another NDK or alter the API target when making a comparable build; record deviations. No vendor MCU binary, vendor firmware, or decompiled vendor artifact is required or included.

## Build

From the repository root, with `NDK_ROOT` set to the installed NDK path:

```sh
python3 scripts/build-android.py --ndk "$NDK_ROOT" --output dist/android-arm64
```

The helper's default output is `dist/android-arm64`. It produces the two ARM64 executables and metadata/notices; inspect those exact generated notices before redistribution. Do not describe this as a reproducible or reproducible-bit-for-bit build unless independent clean builds have been compared.

This is a build step only. It does not authorize installation, enable root access, create a listener, start a service, or provide recovery. Do not copy an executable to a ScreenMate as a deployment procedure. See [compatibility](compatibility.md), [boot and recovery](boot-and-recovery.md), and [security](security.md).

## Source and license boundaries

Project source is MIT-licensed (see `LICENSE`). The binaries are built from this repository's own source. Build-tool/runtime notices are included in the output archive; see [third-party notices](../THIRD_PARTY_NOTICES.md). No third-party vendor firmware or executable is redistributed.
