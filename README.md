# ScreenMate receive-only MCU → Panda UDP (experimental source)

This is an **experimental, source-only** investigation of a receive-only CAN stream from one TeslaLogic ScreenMate firmware to Panda-compatible UDP clients. It is **not a universal ScreenMate modification**. There is **no installer**, boot service, APK, prebuilt binary, vendor binary, or one-click procedure in this repository. Do not deploy it to a vehicle from these files alone.

## What has actually been tested

On **one** rooted ScreenMate in a parked, stationary, unoccupied, not-drive-ready Model 3, a guarded instance coexisted with the stock MCU/VHAL stream. On a verified boot, an independent subscribed 60-second UDP 1338 probe received 176,511 valid frames, zero malformed, including 2,052 bus-0/DLC-8 `0x401` frames. The MCU/VHAL processes and the stock ashmem FD remained present, and reader telemetry reported zero rejects/overruns during that sample. UDP 1338 was bound only to `ap0` and `tun1`, not cellular or wildcard.

Those observations do **not** prove lossless coverage, compatibility with another ScreenMate/firmware, post-boot behavior in any user's Panda client or vehicle display, or recovery after a forced reader/watchdog failure. A private guarded fallback existed in that one-device trial, but restoration was **not fault-tested** on the verified boot and its device-specific scripts are intentionally **not published** here.

## Safety and scope

- The native code is receive-only: no CAN TX, filter changes to the stock VHAL, MCU reset, or intentional stock session close. Raw reading is disabled by default; opening requires explicit `--enable-mcu-raw` and mode flags. Production port 1338 additionally requires `--stock-present --production-cutover`.
- `--check-stock-only` is a no-open `/proc` preflight. **Never use the vendor MCU `dumpsys` endpoint** on the tested firmware: it has coincided with a SIGPIPE service exit.
- The code pins the tested MCU executable SHA-256 (`18846138ffe5333e3be636e3bfb91d9b2e4b046fb2107988ed13cb71e8d99617`) and assumes specific TeslaLogic Binder transactions, ashmem layout, service paths, and process-FD behavior. That hash is a compatibility *refusal gate*, **not** permission to edit it until another firmware has been independently analyzed and tested.
- Android root and access to the vendor Binder service are required for device use. No cellular listener is intended. A working host test does not validate a vehicle or recovery path.
- Any future device test should be separately approved while parked, stationary, unoccupied, and not drive-ready; protect the stock MCU/VHAL session and verify an independent subscribed client rather than count a UDP handshake as delivery. Do not deliberately interrupt the stock service to test this repository.

## Contents

- `native/raw_mcu_bridge.cpp` and headers: guarded MCU stream reader and Panda UDP encoding.
- `native/panda_bridge.cpp`: original filtered-stock-ring UDP bridge source for comparison and host tests.
- `build/probe_raw_udp.py`: bounded, metadata-only UDP subscriber (counts bus/ID/DLC; never logs CAN payloads). Example against a **test** listener: `python3 build/probe_raw_udp.py 127.0.0.1 1339 10`.
- `tests/`: synthetic host tests for ring decoding, ownership guards, CLI gates, and Panda UDP framing. These tests never open a real MCU session.

No TeslaLogic executable, decompiled/disassembled vendor material, device logs, personal network address, or executable installer is included.

## Host verification

Requires Python 3 and a C++17 compiler on Linux. Tests build throwaway executables under ignored `build/`:

```sh
mkdir -p build
python3 -m unittest discover -s tests -p 'test_*.py' -q
for name in raw_mcu_ring stock_guard session_inventory proc_fd_inventory; do
  g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread "tests/test_${name}.cpp" -o "build/test_${name}"
  "build/test_${name}"
done
```

The Android source previously built for ARM64 with NDK r28c targeting API 34 and `binder_ndk`, `log`, `dl`, and `android` libraries. **There is no verified clean-room Android release build or device installer in this public export.** Do not copy a host test binary to ScreenMate. Before any installable release, add a reproducible cross-build, supported-firmware allowlist and read-only compatibility preflight, an independently reviewed atomic installer/uninstaller, and verified supervised fallback on more than one device.

## License

The original source in this repository is offered under the MIT License. TeslaLogic and Tesla are third parties; this project is not affiliated with or endorsed by either. No third-party vendor firmware or binaries are licensed or distributed here.
