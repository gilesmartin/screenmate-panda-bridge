# Security and safety

## Threat model and limits

This is experimental vehicle-adjacent software. Receive-only means no CAN TX is implemented; it does **not** mean risk-free, lossless, or suitable for unsupervised operation. Raw MCU reading creates a separate wildcard receive session and depends on firmware-specific Binder/ashmem/process behavior. A manual raw reader without supervision is dangerous. No installer, service, or recovery mechanism is supplied. Do not deploy based on binaries alone.

The hash gate pins exactly one MCU executable (`18846138ffe5333e3be636e3bfb91d9b2e4b046fb2107988ed13cb71e8d99617`). Never edit the pin to bypass an unknown firmware refusal. Never use vendor MCU `dumpsys` for inspection: it has coincided with a SIGPIPE service exit on the tested firmware. Do not run `--check-stock-only` while a raw reader is active; the preflight assumes its own no-reader FD state.

Raw mode uses explicit gates (`--enable-mcu-raw` and a selected stock mode); production port 1338 additionally requires `--stock-present --production-cutover`. These guards are not permission to launch it or proof of safe recovery. Do not share runnable raw-open, service restart, or cutover commands. Stock-session safety and process/FD checks are firmware-specific. Never intentionally interrupt the stock MCU/VHAL service to test fallback.

## Network privacy

The intended UDP listener binds only to `ap0,tun1`; there is no public, cellular, or wildcard listener intended. The software does not establish a VPN or configure trusted networking. The listener protocol is unauthenticated and unencrypted; any reachable peer can send registration/control datagrams and observe frames sent to its registered endpoint. Use only on a trusted local network or VPN configured by the owner. Do not expose it to the Internet or an untrusted network.

The probe reports metadata (bus, CAN ID, DLC, counts) and does not print payloads. Still treat logs as potentially identifying. Never publish private hostnames/domains/IPs/MACs/UIDs, owner contact details, identifying logs, or firmware/vendor blobs.

## Physical and operational scope

No physical Panda USB/OBD passthrough, CAN TX, firmware flashing, or diagnostic operation is supported. Use any separately approved device evaluation only while parked, stationary, unoccupied, and not drive-ready, and with owner-reviewed supervision and recovery. See [boot and recovery](boot-and-recovery.md), [compatibility](compatibility.md), and [validation](validation.md).
