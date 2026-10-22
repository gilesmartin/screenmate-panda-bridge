# Validation and evidence

Keep these evidence classes separate; success in one does not imply success in another.

## 1. Host tests

The repository's Python suite and four C++ tests exercise synthetic fixtures and host-side logic; tests do not open a real MCU session. Run the maintained test entry point from the repository root:

```sh
scripts/test-host.sh
```

The live CI configuration is the authoritative source for current host-test results; see the repository's Actions/CI status. Do not copy a test binary to a device.

## 2. Android cross-build

The Android ARM64 cross-build produces project-owned binaries plus metadata and NDK notices; it performs no device action. A successful cross-build is not installation, runtime compatibility, or physical validation. See [build](build.md).

## 3. Historical one-device observation

A prior verified-boot trial on one rooted ScreenMate reported, over an independent subscribed 60-second UDP 1338 sample: 176,511 valid frames, zero malformed, 2,052 bus-0/DLC-8 `0x401` frames. Reader telemetry reported zero rejects/overruns; stock MCU/VHAL processes and stock ashmem FD remained present. The listener was bound to `ap0,tun1` rather than wildcard/cellular.

These are historical reported observations, not a fresh reproduction or current liveness check. A user reported tes•LAX operation during that earlier runtime. Post-boot Panda-client behavior has not been verified; other clients have not been verified. The prior private guarded fallback was not fault-tested on the verified boot.

## Not established

This preview does not establish all-bus coverage, lossless UDP under load, recovery/restoration after a fault, compatibility with any other firmware, a clean independent reproduction of the Android release build, or decoded behavior in every client. Host tests, synthetic fixtures, a new community build, historical live observations, and an installed-device test are distinct evidence categories. A successful handshake is not frame delivery or decoding. For protocol limits see [architecture](architecture.md); for read-only compatibility checks see [compatibility](compatibility.md).

No physical Panda USB or OBD passthrough, CAN transmission, firmware flashing, or vehicle diagnostics are supported or proven by this repository. Do not deliberately disrupt a stock service or test on a vehicle that is not parked, stationary, unoccupied, and not drive-ready.

## Community build verification

The release adds no vehicle-facing feature. A clean Linux CI run exposed ignored `ftruncate` results in the filtered bridge PID-file path; those failures now return a controlled error, with a non-seekable-PID-file regression test. CAN decoding, UDP protocol, raw Binder/session gates and firmware pin are unchanged. The new binaries have not been installed on the vehicle.
