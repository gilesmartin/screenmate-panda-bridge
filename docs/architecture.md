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

## Maintainer reference

These implementation and release notes previously appeared in the README. Commands assume the repository root. Start with the [project overview](../README.md) if you are deciding whether to use the project.

An experimental, receive-only bridge for one ScreenMate MCU firmware to Panda-compatible UDP clients. **Compatibility is one firmware fingerprint only; this is not a universal ScreenMate modification.** This is a developer preview, not a supported vehicle product.

> **Shareable description:** Experimental receive-only ScreenMate Panda UDP bridge; one firmware only; check compatibility first; no installer and no CAN TX.

### Start here

- [Architecture and data flow](architecture.md)
- [Compatibility and read-only checker](compatibility.md)
- [Build the Android ARM64 binaries](build.md)
- [Panda client setup and safe probe](client-setup.md)
- [Boot, supervision, and recovery status](boot-and-recovery.md)
- [What has and has not been validated](validation.md)
- [Security and safety limitations](security.md)
- [Third-party notices](../THIRD_PARTY_NOTICES.md)

### Important before use

There is **no APK, Magisk module, installer, auto-install/boot script, or one-click procedure**. The release artifacts are source and an Android ARM64 archive containing two project-built executables plus build metadata and notices. They are not an installable package. Do not deploy from these files alone: use requires an owner-specific, independently reviewed lifecycle/supervision and recovery design. This repository does not supply one.

The raw MCU path is especially dangerous if run manually without supervision. It is disabled by default and requires explicit gates. It is receive-only (no CAN transmit), but that does not make the operation safe or lossless. See [security](security.md) and [boot/recovery](boot-and-recovery.md).

### Quick start for developers

1. Read [compatibility](compatibility.md). Do not infer that a matching host build or protocol handshake authorizes device installation.
2. Run the [offline host tests](validation.md) and follow [build instructions](build.md) if developing.
3. Only connect a client to an already-authorized, owner-managed listener. The [client guide](client-setup.md) includes a metadata-only probe example using a placeholder host; it does not create or enable a listener.

The raw reader permits only `ap0,tun1`; the stock bridge defaults to those interfaces; it does not establish a network/VPN. UDP has no authentication or encryption. No public, cellular, or wildcard listener is intended.

The historical guarded fallback was **not fault-tested** on the verified boot; it is not supplied as a portable recovery guarantee.

### Scope and evidence

The stock bridge reads the existing read-only VHAL ring and applies Panda peer-side filters; it cannot restore frames already filtered upstream, including `0x401`. The optional raw reader opens its own wildcard MCU receive session; it preserves received CAN ID, bus, and DLC and forces all frames to registered peers. See [architecture](architecture.md).

Historically, one rooted device trial reported 176,511 valid frames, zero malformed, and 2,052 bus-0/DLC-8 `0x401` frames in a 60-second sample. This is historical evidence, not a current liveness check, clean-room release build, or guarantee. A user reported tes•LAX operation during that earlier runtime; post-boot Panda client operation and other clients have not been verified. See [validation](validation.md).

### License

Original project source: MIT; see [LICENSE](../LICENSE). Third-party attribution and distribution limitations are in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
