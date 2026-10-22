# Compatibility and read-only preflight

## One firmware fingerprint only

This preview is gated to the exact tested MCU executable SHA-256:

`18846138ffe5333e3be636e3bfb91d9b2e4b046fb2107988ed13cb71e8d99617`

The pin is a refusal gate, not an invitation to edit it. The implementation also depends on TeslaLogic-specific Binder transactions, ashmem layout, service paths, and process/FD behavior. A mismatch means unsupported: stop; do not bypass the check. No other ScreenMate model/firmware is established as compatible.

## Checker

`scripts/compatibility.py` is a small read-only compatibility report. Its device mode gathers shell hash/property/root/interfaces/ABI information only; it does not request vendor dumpsys, open a session, change network configuration, or approve installation. Use it only for an owner-authorized, already-connected device under the project owner's safe review process. This guide intentionally does not provide an ADB connection or deployment command. For offline development/tests, use its JSON fixture mode. For an existing authorized root ADB transport, run this **read-only** report (replace the placeholder serial; it never connects/reconfigures a device):

```sh
python3 scripts/compatibility.py --serial YOUR_AUTHORIZED_DEVICE
```

Use `--adb /path/to/adb` if ADB is not on your PATH. It reports a firmware fingerprint and baseline prerequisites, not a verified Binder/session contract or lifecycle approval. `ready_for_install` and `may_open_session` are always false. Exit 0 means only that the sampled baseline fields match; 3 means not established; 4 means evidence unavailable.

The report identifies a supported binary/protocol fingerprint; it is **not** an approval-to-install signal, a complete system safety audit, or proof that runtime handover/recovery works. Current device liveness is not asserted here.

## Compatibility facts and limits

- Supported raw interface names are exactly `ap0,tun1`. The native raw code rejects a different interface set; do not rely on an alternate-interface flag without a separate review.
- The stock bridge sees only frames already published into the VHAL ring; its peer-side Panda filters cannot recover upstream-filtered identifiers such as `0x401`.
- Raw mode opens its own wildcard MCU receive session. Guard, ownership, and process checks are firmware-specific safeguards. They do not support unknown firmware or guarantee lossless delivery.
- UDP 1338's protocol handshake is only registration. It does not show that frames arrived, that an application decoded them, or that a vehicle display updated.

For architecture and protocol detail, see [architecture](architecture.md); for evidence boundaries, see [validation](validation.md). Read [security](security.md) before any use.
