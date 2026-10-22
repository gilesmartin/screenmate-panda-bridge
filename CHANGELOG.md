# Changelog

## screenmate-preview-2026.10.22 — experimental developer preview

- Document what the Panda UDP emulator does and does not emulate, the raw-versus-filtered paths, firmware gates, client registration, historical boot integration and remaining recovery limits.
- Add pinned NDK r28c ARM64/API34 build tooling, complete installed NDK notices in generated artifacts, source hashes and checksums. The build never installs or runs target binaries.
- Add a narrow read-only fingerprint/prerequisite checker; a match is not deployment permission or complete compatibility validation.
- Add Linux fixture CI and Android cross-build CI. CAN parsing, UDP framing, Binder and ownership guards remain unchanged from the original public source export. The filtered bridge now checks PID-file truncation failures (Linux fortified-compiler finding) instead of ignoring them.
- No installer, Magisk module, CAN TX, new vehicle tests or production device changes.
