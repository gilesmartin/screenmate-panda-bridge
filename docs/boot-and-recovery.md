# Boot lifecycle and recovery

## Not an Android service package

This repository ships no installer, Magisk module, boot script, auto-start configuration, or device-specific wrapper. It makes no generic Android boot guarantee. Building the binaries does not provide a safe startup, supervision, cutover, or rollback lifecycle. **Do not deploy or manually run a binary from these files alone.** Any future deployment must be separately approved for the specific device and include an owner-specific, independently reviewed lifecycle/supervision and recovery plan.

Magisk root/runtime is distinct from bridge boot integration. The separate upstream project [screenmate-magisk-runit](https://github.com/gilesmartin/screenmate-magisk-runit) concerns the Magisk root/runtime boot chain; it does not enable this Panda bridge, and this repository does not inherit its boot guarantees. The historical device-specific wrappers are intentionally not published.

## Historical lifecycle evidence (not a template)

In a prior one-device investigation, a Magisk `service.d` hook did **not** run at `late_start`. A private fallback used a protected, hash-checked detached hook under `/data/adb/post-fs-data.d`, waited for `sys.boot_completed`, limited itself to one attempt per boot, checked a stock baseline, and used supervised handover with a PID-owned logger/watchdog monitor and guarded stock restoration. Those private scripts are not included and should not be reconstructed from this prose. Restoration was not fault-tested on the verified boot. This history is not a supported recipe or assurance of recoverability.

The native `--check-stock-only` path is a no-open `/proc` preflight and assumes no raw reader is active; do not run it if a raw reader already exists. Never call the vendor MCU `dumpsys` endpoint; it coincided with a SIGPIPE service exit on the tested firmware. Do not deliberately interrupt/restart stock services to test recovery.

See [compatibility](compatibility.md) for the read-only report, [validation](validation.md) for evidence boundaries, and [security](security.md) for device and transport risks.
