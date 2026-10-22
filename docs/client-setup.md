# Panda client setup

This guide is for a client connecting to an **already-authorized, already-running** listener managed by the device owner. It does not install or start the bridge, grant root, configure a VPN/network, or establish that the firmware is compatible. Do not proceed from build artifacts alone; first read [compatibility](compatibility.md), [boot/recovery](boot-and-recovery.md), and [security](security.md).

## Listener and client protocol

The intended listener is UDP 1338 on `ap0,tun1` (not wildcard or cellular). The project does not create these interfaces or configure a path to them. Any transport/VPN is user-configured and is not automatically established by this project. The listener has no authentication or encryption; only use a trusted local network or VPN and a host/IP supplied by the device owner.

Panda clients generally begin with `hello` (v1) or `ehllo` (v2). A v2 peer is filtered by default in the stock bridge; clients can request all frames or specify peer filters. Raw mode forces all frames to registered peers. The project bridge expires peer registration after 10 seconds without heartbeat/control traffic. Follow the client application's own supported setup; do not interpret an acknowledgment as proof of data delivery.

## Safe metadata-only probe

The repository provides `build/probe_raw_udp.py`, a bounded subscriber that sends safe subscription metadata and reports counts by bus/ID/DLC without recording payloads. Example for a listener the owner has already authorized and provided:

```sh
python3 build/probe_raw_udp.py <OWNER_PROVIDED_HOST> 1338 10
```

Replace the placeholder with the approved endpoint. This command does **not** start the listener. Do not scan for hosts, use a public endpoint, or publish identifying addresses/logs. Treat the output as diagnostic metadata. A probe count is not proof of losslessness or decoded client behavior.

## Interpretation

Handshake acknowledgment means only that the peer registered. To establish frame receipt, observe frame counts independently; to establish application decoding, verify within the specific client. Historic tes•LAX user operation was reported during a prior runtime, but post-boot Panda-client behavior and other clients remain unverified. See [validation](validation.md).
