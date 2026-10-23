# ScreenMate Panda Bridge

Use the CAN data already available inside ScreenMate with a Panda-compatible app, such as tes•LAX, over the local network.

The receive-only bridge reads vehicle data from ScreenMate and sends it in the UDP format those apps expect. It does not transmit commands onto the car's CAN bus, and it does not emulate a physical Panda USB device.

## What does it add?

There are two readers. The stock reader uses the data ScreenMate already exposes. The optional raw reader can receive additional messages that the stock feed leaves out. The [technical guide](docs/architecture.md) explains the difference.

tes•LAX worked during the earlier owner's test. That is not a promise that every Panda-compatible app or ScreenMate firmware will work.

## Can I install it?

This is an experimental developer release. It requires a rooted ScreenMate and the tested MCU firmware. There is no installer: the download contains source and Android ARM64 executables, not an APK or Magisk module.

The raw reader needs a reviewed startup, supervision and shutdown setup. Do not run it manually just because the firmware check passes. Automatic fallback recovery was not fault-tested, and these newly built release binaries have not been installed or tested in the car.

## Where to start

1. Read the [compatibility requirements and read-only checker](docs/compatibility.md).
2. See the [downloads](https://github.com/gilesmartin/screenmate-panda-bridge/releases/tag/screenmate-preview-2026.10.22) or [build instructions](docs/build.md).
3. Use the [client setup guide](docs/client-setup.md) only after you have an approved, running listener.

Keep the feed on a trusted local network. It is not authenticated or encrypted. Do not expose it to the internet or invoke the vendor MCU `dumpsys` endpoint.

[Boot and recovery notes](docs/boot-and-recovery.md) · [Safety and security](docs/security.md) · [Tests and vehicle observations](docs/validation.md) · [Technical details](docs/architecture.md)

## License

Original source is [MIT licensed](LICENSE). The build includes its NDK notices; see [third-party notices](THIRD_PARTY_NOTICES.md).
