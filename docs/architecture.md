# Architecture and data flow

## Two independent receive paths

`native/panda_bridge.cpp` passively locates and maps the existing VHAL ashmem CAN ring read-only. It decodes ring slots and emits Panda-format UDP frames. The bridge does not call the MCU Binder service, modify the VHAL filters, transmit CAN, reset the MCU, or manage a vendor session.

Its protocol listener defaults to UDP **1338** on `ap0,tun1`. Interfaces are exact by default, and these are the only supported runtime names in the native raw implementation. Do not assume an alternate-interface option is supported for raw use without code review. Interface binding failure means no listener on that interface.

## Optional raw MCU reader

`native/raw_mcu_bridge.cpp` is a separate, opt-in reader. It opens its own wildcard MCU receive session and reads the returned shared-memory ring; it does not send CAN frames. It preserves received CAN ID, bus, and DLC in Panda packets and does not alter stock VHAL filtering. The code applies process/FD ownership guards, a firmware hash gate, and a monitor; those are safeguards, not a proof of safe operation or a replacement for an independently supervised lifecycle.

Raw mode is disabled unless explicitly enabled. `--enable-mcu-raw` must be paired with exactly one mode gate: diagnostic `--allow-absent-stock`, guarded stock-present mode (`--stock-present`, port 1339), or an explicit stock session ID path. Port 1338 production mode additionally requires `--stock-present --production-cutover`. These are source-level facts, **not runnable deployment instructions**; manual unsupervised activation is dangerous. Do not use guessed stock IDs or change the MCU hash pin to bypass refusal.

`--check-stock-only` is a no-open `/proc` preflight. It assumes no raw reader is already active and must not be used when one is active. Neither path should query the vendor MCU `dumpsys` endpoint: on the tested firmware that coincided with a SIGPIPE service exit.

## Panda UDP behavior

- Version 1 `hello` receives a Panda acknowledgment packet: CAN ID 6, bus 15, DLC 0.
- Version 2 `ehllo` also receives that acknowledgment. It starts peer-filtered in the stock bridge. `0x0c` requests all frames for that peer; `0x0f` adds filters. Raw mode forces all received frames to each registered peer, regardless of peer filters.
- A peer registration expires after 10 seconds without heartbeat/control traffic. A successful handshake proves only protocol registration, not that frames arrive or that an application decodes them.
- The bounded `build/probe_raw_udp.py` sends only safe subscription metadata and counts metadata; it never prints CAN payloads.

Stock bridge peer filters act only on frames that reached the VHAL ring (about 75 IDs in the observed setup); they cannot recover upstream-filtered `0x401`. The raw reader is the separate path that observed `0x401` in the historical sample.

## Trust boundaries

The listener has no UDP authentication or encryption. Interface binding limits the intended exposure but does not make peers trusted. See [security](security.md), [compatibility](compatibility.md), and [boot/recovery](boot-and-recovery.md) before considering any device use.
