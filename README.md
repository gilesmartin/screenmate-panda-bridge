# ScreenMate Panda Bridge — experimental developer preview

An experimental, receive-only bridge for one ScreenMate MCU firmware to Panda-compatible UDP clients. **Compatibility is one firmware fingerprint only; this is not a universal ScreenMate modification.** This is a developer preview, not a supported vehicle product.

> **Shareable description:** Experimental receive-only ScreenMate Panda UDP bridge; one firmware only; check compatibility first; no installer and no CAN TX.

## Start here

- [Architecture and data flow](docs/architecture.md)
- [Compatibility and read-only checker](docs/compatibility.md)
- [Build the Android ARM64 binaries](docs/build.md)
- [Panda client setup and safe probe](docs/client-setup.md)
- [Boot, supervision, and recovery status](docs/boot-and-recovery.md)
- [What has and has not been validated](docs/validation.md)
- [Security and safety limitations](docs/security.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## Important before use

There is **no APK, Magisk module, installer, auto-install/boot script, or one-click procedure**. The release artifacts are source and an Android ARM64 archive containing two project-built executables plus build metadata and notices. They are not an installable package. Do not deploy from these files alone: use requires an owner-specific, independently reviewed lifecycle/supervision and recovery design. This repository does not supply one.

The raw MCU path is especially dangerous if run manually without supervision. It is disabled by default and requires explicit gates. It is receive-only (no CAN transmit), but that does not make the operation safe or lossless. See [security](docs/security.md) and [boot/recovery](docs/boot-and-recovery.md).

## Quick start for developers

1. Read [compatibility](docs/compatibility.md). Do not infer that a matching host build or protocol handshake authorizes device installation.
2. Run the [offline host tests](docs/validation.md) and follow [build instructions](docs/build.md) if developing.
3. Only connect a client to an already-authorized, owner-managed listener. The [client guide](docs/client-setup.md) includes a metadata-only probe example using a placeholder host; it does not create or enable a listener.

The raw reader permits only `ap0,tun1`; the stock bridge defaults to those interfaces; it does not establish a network/VPN. UDP has no authentication or encryption. No public, cellular, or wildcard listener is intended.

The historical guarded fallback was **not fault-tested** on the verified boot; it is not supplied as a portable recovery guarantee.

## Scope and evidence

The stock bridge reads the existing read-only VHAL ring and applies Panda peer-side filters; it cannot restore frames already filtered upstream, including `0x401`. The optional raw reader opens its own wildcard MCU receive session; it preserves received CAN ID, bus, and DLC and forces all frames to registered peers. See [architecture](docs/architecture.md).

Historically, one rooted device trial reported 176,511 valid frames, zero malformed, and 2,052 bus-0/DLC-8 `0x401` frames in a 60-second sample. This is historical evidence, not a current liveness check, clean-room release build, or guarantee. A user reported tes•LAX operation during that earlier runtime; post-boot Panda client operation and other clients have not been verified. See [validation](docs/validation.md).

## License

Original project source: MIT; see [LICENSE](LICENSE). Third-party attribution and distribution limitations are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
